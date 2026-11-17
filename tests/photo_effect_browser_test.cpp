#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QColor>
#include <QElapsedTimer>
#include <QUrl>
#include <QRectF>
#include <QSettings>
#include <QTemporaryDir>
#include <QDir>
#include <QStringList>
#include <QVariantMap>
#include <QKeyEvent>
#include <QTest>
#include <iostream>
#include <memory>
#include <QSet>
#include <QImage>
#include <functional>
#include <algorithm>
#include <cmath>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName(QStringLiteral("LooksyEffectsUI"));
  app.setApplicationName(QStringLiteral("PhotoEffectStackBrowserTest"));
  QTemporaryDir settings_directory;
  if (!settings_directory.isValid()) return 1;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_directory.path());
  { QSettings isolated_settings; isolated_settings.clear(); }
  QImage thumbnail(192, 120, QImage::Format_ARGB32_Premultiplied);
  thumbnail.fill(QColor(QStringLiteral("#7458A6")));
  const auto thumbnail_path = settings_directory.path() + QStringLiteral("/effect-example.png");
  if (!thumbnail.save(thumbnail_path)) return 1;
  QImage original(192, 120, QImage::Format_ARGB32_Premultiplied);
  original.fill(QColor(QStringLiteral("#283849")));
  const auto original_path = settings_directory.path() + QStringLiteral("/original-example.png");
  if (!original.save(original_path)) return 1;
  QImage look_thumbnail(192, 120, QImage::Format_ARGB32_Premultiplied);
  look_thumbnail.fill(QColor(QStringLiteral("#7B513B")));
  const auto look_thumbnail_path = settings_directory.path() + QStringLiteral("/look-example.png");
  if (!look_thumbnail.save(look_thumbnail_path)) return 1;
  QQmlEngine engine;
  engine.rootContext()->setContextProperty("appShell", QVariantMap{{"interfaceScale", 1.0}});
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../apps/desktop/qml/components"
Item {
    id: harness
    width: 340; height: 820
    signal assetChanged()
    property bool draft: false
    property int applied: 0
    property int cancelled: 0
    property int sizeEdits: 0
    property int lookApplications: 0
    property bool reject: false
    property bool hasSavedEffect: false
    property bool hasSecondEffect: false
    property bool effectEnabled: true
    property real effectAmount: 0.4
    property string draftType: ""
    property int bypassEdits: 0
    property int moveCalls: 0
    property int lastMoveTo: -1
    property int amountEdits: 0
    property string lastAmountId: ""
    property real lastAmount: 0
    property int coalescedBegins: 0
    property int coalescedEnds: 0
    property int maskEditRequests: 0
    property string editedMaskSetId: ""
    property string mockThumbnailSource: ""
    property string mockOriginalSource: ""
    property string mockLookThumbnailSource: ""
    function effectExampleThumbnailSource(type, look) {
        return look ? mockLookThumbnailSource : mockThumbnailSource
    }
    function exampleThumbnailOriginalSource() {
        return mockOriginalSource
    }
    function availablePhotoEffects() {
        const rows = [{type: "film_grain", name: "Film grain", category: "Film", description: "Texture"},
            {type: "gaussian_blur", name: "Gaussian blur", category: "Blur", description: "Soften"},
            {type: "box_blur", name: "Box blur", category: "Blur", description: "Average nearby pixels"}]
        const categories = ["Color", "Texture", "Geometry", "Lighting"]
        for (let index = 0; index < 12; ++index) {
            rows.push({type: "sample_effect_" + index, name: "Sample effect " + (index + 1),
                category: categories[index % categories.length], description: "A photographic example for gallery virtualization."})
        }
        return rows
    }
    function availablePhotoEffectLooks() {
        return [{id: "dream_haze", name: "Dream Haze", category: "Optical", description: "Diffuse bright details"}]
    }
    function applyPhotoLayerEffectLook(index, id) {
        if (reject || id !== "dream_haze") return false
        lookApplications++
        return true
    }
    function photoLayerEffects(index) {
        if (hasSavedEffect) {
            const rows = [{id: "saved-effect",type:"gaussian_blur",version:1,
                enabled:effectEnabled,amount:effectAmount,sigma:8,edgeMode:0,maskId:"effect-mask"}]
            if (hasSecondEffect) rows.push({id:"second-effect",type:"film_grain",version:1,
                enabled:true,amount:0.65,grainStrength:0.04,grainSize:1.2,seed:13,maskId:""})
            return rows
        }
        return draft ? [{id:"draft-effect",type:draftType,version:1,enabled:true,amount:1,sigma:8,edgeMode:0}] : []
    }
    function setPhotoLayerEffectEnabled(index, id, enabled) {
        if (index !== 0 || id !== "saved-effect") return false
        effectEnabled = enabled
        bypassEdits++
        return true
    }
    function previewPhotoLayerEffect(index, type) { draft = !reject; draftType = type; return draft }
    function finishPhotoLayerEffectPreview(apply) {
        if (!draft) return false
        draft = false
        if (apply) applied++; else cancelled++
        return true
    }
    function setPhotoLayerGaussianBlur(index, id, size, amount, enabled) { sizeEdits++; return true }
    function setPhotoLayerEffectAmount(index, id, amount) {
        if (index !== 0 || id !== "saved-effect") return false
        amountEdits++
        lastAmountId = id
        lastAmount = amount
        effectAmount = amount
        return true
    }
    function beginCoalescedEdit() { coalescedBegins++; return true }
    function endCoalescedEdit() { coalescedEnds++; return true }
    function movePhotoLayerEffect(index, id, target) {
        if (index !== 0 || !hasSecondEffect || target < 0 || target > 1) return false
        moveCalls++
        lastMoveTo = target
        return true
    }
    Theme { id: theme }
    ModularToolPanel {
        id: toolPanel
        objectName: "effectsToolPanelFixture"
        anchors.fill: parent
        active: true
        title: qsTr("Filters")
        targetText: qsTr("Selected layer · Violet paint")
        contextKey: "photo-effect-ui-proof"
        ColumnLayout {
            parent: toolPanel.contentItem
            Layout.fillWidth: true
            spacing: theme.space12
            PhotoEffectStack {
                objectName: "stack"
                Layout.fillWidth: true
                controller: harness
                maskEditHandler: function(index, setId) {
                    if (index !== 0) return false
                    harness.maskEditRequests++
                    harness.editedMaskSetId = setId
                    return true
                }
                layerIndex: 0
                layerData: harness.hasSavedEffect || harness.draft
                    ? {displayName:"Violet paint",enabled:harness.effectEnabled} : null
            }
        }
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EFFECT_TEST_ROOT "/tests/effect-browser-harness.qml")));
  std::unique_ptr<QObject> root(component.create());
  if (!root) { std::cerr << component.errorString().toStdString(); return 1; }
  root->setProperty("mockThumbnailSource", QUrl::fromLocalFile(thumbnail_path).toString());
  root->setProperty("mockOriginalSource", QUrl::fromLocalFile(original_path).toString());
  root->setProperty("mockLookThumbnailSource", QUrl::fromLocalFile(look_thumbnail_path).toString());
  QQuickWindow window;
  window.resize(340, 820);
  auto* root_item = qobject_cast<QQuickItem*>(root.get());
  if (!root_item) { std::cerr << "Effects fixture root is not a Quick item\n"; return 1; }
  root_item->setWidth(340);
  root_item->setHeight(820);
  root_item->setParentItem(window.contentItem());
  window.show();
  window.requestActivate();
  const auto settle = []() {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 250) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  };
  const auto evidence_directory = qEnvironmentVariable("PHOTO_EFFECT_EVIDENCE_DIR");
  if (!evidence_directory.isEmpty() && !QDir().mkpath(evidence_directory)) {
    std::cerr << "Could not create effect evidence directory\n";
    return 1;
  }
  const auto capture = [&](const QString& name) {
    if (evidence_directory.isEmpty()) return true;
    QTest::mouseMove(&window, QPoint(window.width() - 4, window.height() - 4), 20);
    settle();
    const auto target = evidence_directory + "/" + name;
    if (!window.grabWindow().save(target)) {
      std::cerr << "Could not capture " << target.toStdString() << '\n';
      return false;
    }
    return true;
  };
  settle();
  if (QGuiApplication::platformName() != QStringLiteral("offscreen") && !window.isExposed()) {
    std::cerr << "Native effect capture requires an exposed window\n";
    return 1;
  }
  if (!capture(QStringLiteral("effects-empty.png"))) return 1;
  const auto find_from = [&](QObject* start, const char* name) {
    QSet<QObject*> visited;
    std::function<QObject*(QObject*)> search = [&](QObject* object) -> QObject* {
      if (!object || visited.contains(object)) return nullptr;
      visited.insert(object);
      if (object->objectName() == QString::fromLatin1(name)) return object;
      for (auto* child : object->children()) if (auto* result = search(child)) return result;
      if (auto* item = qobject_cast<QQuickItem*>(object))
        for (auto* child : item->childItems()) if (auto* result = search(child)) return result;
      return nullptr;
    };
    return search(start);
  };
  const auto find = [&](const char* name) {
    if (auto* result = find_from(root.get(), name)) return result;
    return find_from(window.contentItem(), name);
  };
  const auto click = [&](QObject* object) {
    auto* item = qobject_cast<QQuickItem*>(object);
    if (!item || !item->isVisible() || !item->isEnabled()) {
      std::cerr << "Unavailable effect control "
                << (object ? object->objectName().toStdString() : "missing") << '\n';
      return false;
    }
    for (int pass = 0; pass < 3; ++pass) {
      for (auto* ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (!ancestor->property("contentY").isValid() ||
            !ancestor->property("contentHeight").isValid()) continue;
        const auto rect = item->mapRectToItem(ancestor, item->boundingRect());
        const auto offset = rect.top() < 0 ? rect.top() - 8
            : rect.bottom() > ancestor->height() ? rect.bottom() - ancestor->height() + 8 : 0;
        const auto next = std::clamp(ancestor->property("contentY").toDouble() + offset, 0.0,
            std::max(0.0, ancestor->property("contentHeight").toDouble() - ancestor->height()));
        ancestor->setProperty("contentY", next);
        settle();
      }
    }
    const auto position = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    if (!QRectF(0, 0, window.width(), window.height()).contains(position)) {
      std::cerr << "Effect control lies outside the native viewport: " << object->objectName().toStdString() << '\n';
      return false;
    }
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, position.toPoint());
    settle();
    return true;
  };
  auto* stack = qobject_cast<QQuickItem*>(find("stack"));
  if (!stack) { std::cerr << "Effect stack did not instantiate\n"; return 1; }
  auto* add = find("photoAddEffectButton");
  auto* picker = find("photoEffectPicker");
  auto* choice = find("photoEffectChoice_gaussian_blur");
  auto* apply = find("photoEffectPreviewApply");
  auto* preview_cancel = find("photoEffectPreviewCancel");
  auto* size = find("photoEffectPreviewSize");
  if (!add || !picker || !apply || !preview_cancel || !size) {
    std::cerr << "Browser controls did not instantiate\n"; return 1;
  }
  if (!click(add)) return 1;
  settle();
  if (preview_cancel->property("visible").toBool() || apply->property("visible").toBool()) {
    std::cerr << "Preview actions must stay out of a fresh example gallery\n"; return 1;
  }
  choice = find("photoEffectChoice_gaussian_blur");
  if (!choice) { std::cerr << "Visible gallery cards did not instantiate\n"; return 1; }
  auto* example_card = qobject_cast<QQuickItem*>(choice);
  if (!example_card ||
      example_card->property("thumbnailSource").toString() != root->property("mockThumbnailSource").toString() ||
      example_card->property("originalSource").toString() != root->property("mockOriginalSource").toString() ||
      example_card->property("badgeText").toString() != QStringLiteral("LIVE PREVIEW") ||
      !example_card->property("previewReady").toBool()) {
    std::cerr << "Effect cards must expose real example and original comparison sources\n"; return 1;
  }
  if (!capture(QStringLiteral("effects-gallery-340-scale-1.png"))) return 1;
  window.resize(980, 820);
  root_item->setWidth(980);
  root_item->setHeight(820);
  settle();
  auto* gallery_toggle = find("photoEffectGallerySizeToggle");
  if (!gallery_toggle || !click(gallery_toggle) ||
      gallery_toggle->property("text").toString() != QStringLiteral("Compact gallery")) {
    std::cerr << "Expanded gallery toggle must present a clear compact action\n"; return 1;
  }
  const auto expanded_width = picker->property("width").toDouble();
  auto* wide_box = qobject_cast<QQuickItem*>(find("photoEffectChoice_box_blur"));
  auto* wide_gaussian = qobject_cast<QQuickItem*>(find("photoEffectChoice_gaussian_blur"));
  if (expanded_width < 700 || !wide_box || !wide_gaussian ||
      std::abs(wide_box->mapToScene(QPointF()).y() - wide_gaussian->mapToScene(QPointF()).y()) > 4.0 ||
      std::abs(wide_box->mapToScene(QPointF()).x() - wide_gaussian->mapToScene(QPointF()).x()) < 100.0) {
    std::cerr << "Expanded gallery must use a wide, side-by-side sample grid; width=" << expanded_width << '\n'; return 1;
  }
  if (!capture(QStringLiteral("effects-gallery-760-expanded.png"))) return 1;
  if (!click(wide_gaussian)) return 1;
  const auto preview_width = picker->property("width").toDouble();
  if (picker->property("browsing").toBool() || preview_width >= expanded_width) {
    std::cerr << "Selecting an effect must return the expanded gallery to its compact preview width\n"; return 1;
  }
  auto* compact_cancel = find("photoEffectPreviewCancel");
  if (!compact_cancel || !click(compact_cancel)) return 1;
  root->setProperty("cancelled", 0);
  if (!click(add)) return 1;
  gallery_toggle = find("photoEffectGallerySizeToggle");
  if (!gallery_toggle || gallery_toggle->property("text").toString() != QStringLiteral("Compact gallery") ||
      !click(gallery_toggle) || picker->property("width").toDouble() >= expanded_width) {
    std::cerr << "Compact gallery action must restore the narrower popup\n"; return 1;
  }
  window.resize(340, 820);
  root_item->setWidth(340);
  root_item->setHeight(820);
  settle();
  engine.rootContext()->setContextProperty("appShell", QVariantMap{{"interfaceScale", 2.0}});
  window.resize(260, 820);
  root_item->setWidth(260);
  root_item->setHeight(820);
  settle();
  auto* narrow_card = qobject_cast<QQuickItem*>(find("photoEffectChoice_box_blur"));
  auto* narrow_thumbnail = qobject_cast<QQuickItem*>(find("photoEffectChoice_box_blurThumbnail"));
  auto* narrow_image_frame = qobject_cast<QQuickItem*>(find("photoEffectChoice_box_blurImageFrame"));
  auto* narrow_gallery = qobject_cast<QQuickItem*>(find("photoEffectGalleryList"));
  auto* narrow_gallery_toggle = find("photoEffectGallerySizeToggle");
  auto* narrow_cancel = qobject_cast<QQuickItem*>(find("photoEffectPreviewCancel"));
  auto* narrow_apply = qobject_cast<QQuickItem*>(find("photoEffectPreviewApply"));
  const auto narrow_frame_rect = narrow_image_frame
      ? narrow_image_frame->mapRectToScene(narrow_image_frame->boundingRect()) : QRectF();
  const auto narrow_gallery_rect = narrow_gallery
      ? narrow_gallery->mapRectToScene(narrow_gallery->boundingRect()) : QRectF();
  if (!narrow_card || !narrow_thumbnail || !narrow_thumbnail->isVisible() ||
      !narrow_image_frame || !narrow_gallery || narrow_gallery_rect.isEmpty() ||
      !narrow_gallery_rect.contains(narrow_frame_rect) ||
      !narrow_gallery_toggle || narrow_gallery_toggle->property("text").toString() != QStringLiteral("Expand") ||
      !narrow_cancel || !narrow_apply || narrow_cancel->property("visible").toBool() ||
      narrow_apply->property("visible").toBool()) {
    std::cerr << "A complete sample image must fit the 260-pixel, scale-2 gallery viewport\n"; return 1;
  }
  if (!capture(QStringLiteral("effects-gallery-260-scale-2.png"))) return 1;
  engine.rootContext()->setContextProperty("appShell", QVariantMap{{"interfaceScale", 1.0}});
  window.resize(340, 820);
  root_item->setWidth(340);
  root_item->setHeight(820);
  settle();
  auto* gallery = qobject_cast<QQuickItem*>(find("photoEffectGalleryList"));
  auto* category_picker = find("photoEffectCategoryPicker");
  if (!gallery || !category_picker || find("photoEffectChoice_sample_effect_11") ||
      find("photoEffectLookChoice_dream_haze")) {
    std::cerr << "Offscreen example cards must remain virtualized until their category is shown\n"; return 1;
  }
  const auto wheelToLastGroup = [&](int viewport_height) {
    window.resize(340, viewport_height);
    root_item->setWidth(340);
    root_item->setHeight(viewport_height);
    settle();
    gallery->setProperty("contentY", gallery->property("originY"));
    settle();
    const auto local_position = gallery->mapToScene(QPointF(gallery->width() / 2, gallery->height() / 2)).toPoint();
    QTest::mouseMove(&window, local_position, 10);
    for (int step = 0; step < 250; ++step)
      QTest::wheelEvent(&window, QPointF(local_position), QPoint(0, -120), QPoint(0, -80));
    if (gallery->property("contentY").toDouble() <= 0) return false;
    settle();
    auto* last_card = qobject_cast<QQuickItem*>(find("photoEffectLookChoice_dream_haze"));
    if (!last_card || gallery->property("contentY").toDouble() <= 0) {
      std::cerr << "last look missing=" << (!last_card)
                << " y=" << gallery->property("contentY").toDouble() << '\n';
      return false;
    }
    if (last_card->property("thumbnailSource").toString() != root->property("mockLookThumbnailSource").toString()) {
      std::cerr << "last look source was not loaded: "
                << last_card->property("thumbnailSource").toString().toStdString() << '\n';
      return false;
    }
    const auto card_center = last_card->mapToItem(gallery,
        QPointF(last_card->width() / 2, last_card->height() / 2));
    const auto inside = QRectF(0, 0, gallery->width(), gallery->height()).contains(card_center);
    if (!inside)
      std::cerr << "last look outside viewport center=" << card_center.x() << ',' << card_center.y()
                << " view=" << gallery->width() << 'x' << gallery->height() << '\n';
    const auto top_limit = gallery->property("originY").toDouble();
    const auto bottom_limit = top_limit + std::max(0.0,
        gallery->property("contentHeight").toDouble() - gallery->height());
    const auto bottom_position = gallery->property("contentY").toDouble();
    if (bottom_position < top_limit - 1.0 || bottom_position > bottom_limit + 1.0) return false;
    for (int step = 0; step < 250; ++step)
      QTest::wheelEvent(&window, QPointF(local_position), QPoint(0, 120), QPoint(0, 80));
    settle();
    auto* first_card = qobject_cast<QQuickItem*>(find("photoEffectChoice_box_blur"));
    if (!first_card) return false;
    const auto first_center = first_card->mapToItem(gallery,
        QPointF(first_card->width() / 2, first_card->height() / 2));
    const bool returned_to_start = QRectF(0, 0, gallery->width(), gallery->height()).contains(first_center) &&
        gallery->property("contentY").toDouble() <= gallery->property("originY").toDouble() + 1.0;
    return inside && returned_to_start;
  };
  if (!wheelToLastGroup(820)) {
    if (!evidence_directory.isEmpty())
      window.grabWindow().save(evidence_directory + QStringLiteral("/effects-gallery-wheel-failure.png"));
    auto* outer_view = find("photoEffectPickerBodyScrollView");
    auto* outer_content = outer_view ? qobject_cast<QQuickItem*>(outer_view->property("contentItem").value<QObject*>()) : nullptr;
    std::cerr << "Pointer wheel must reach the last virtual gallery category in the 820-pixel viewport; y="
              << gallery->property("contentY").toDouble() << " content=" << gallery->property("contentHeight").toDouble()
              << " height=" << gallery->height() << " outerY="
              << (outer_content ? outer_content->property("contentY").toDouble() : -1)
              << " outerContent=" << (outer_content ? outer_content->property("contentHeight").toDouble() : -1)
              << " outerHeight=" << (outer_content ? outer_content->height() : -1)
              << " lookGroup=" << (find("photoEffectLookGroup_Optical") != nullptr)
              << " lookIntro=" << (find("photoEffectLooksIntroduction") != nullptr)
              << " blur=" << (find("photoEffectGroup_Blur") != nullptr)
              << " color=" << (find("photoEffectGroup_Color") != nullptr)
              << " film=" << (find("photoEffectGroup_Film") != nullptr)
              << " geometry=" << (find("photoEffectGroup_Geometry") != nullptr)
              << " lighting=" << (find("photoEffectGroup_Lighting") != nullptr)
              << " texture=" << (find("photoEffectGroup_Texture") != nullptr) << '\n'; return 1;
  }
  gallery->setProperty("contentY", gallery->property("originY"));
  if (!wheelToLastGroup(650)) {
    std::cerr << "Pointer wheel must reach the last virtual gallery category in the 650-pixel viewport\n"; return 1;
  }
  gallery->setProperty("contentY", gallery->property("originY"));
  window.resize(340, 820);
  root_item->setWidth(340);
  root_item->setHeight(820);
  settle();
  const auto category_model = category_picker->property("model").toStringList();
  const auto lighting_index = category_model.indexOf(QStringLiteral("Lighting"));
  if (lighting_index <= 0 ||
      !QMetaObject::invokeMethod(category_picker, "activated", Q_ARG(int, lighting_index))) return 1;
  settle();
  auto* distant_card = qobject_cast<QQuickItem*>(find("photoEffectChoice_sample_effect_11"));
  if (!distant_card || distant_card->property("thumbnailSource").toString() != root->property("mockThumbnailSource").toString()) {
    std::cerr << "Filtering to a visible effect group must load that group’s example source\n"; return 1;
  }
  QMetaObject::invokeMethod(category_picker, "activated", Q_ARG(int, 0));
  settle();
  choice = find("photoEffectChoice_gaussian_blur");
  if (!choice) { std::cerr << "Restoring all gallery groups did not recreate Gaussian blur\n"; return 1; }
  if (!capture(QStringLiteral("effects-browser.png"))) return 1;
  if (!picker->property("visible").toBool() || apply->property("enabled").toBool()) return 1;
  if (picker->property("modal").toBool() || picker->property("dim").toBool()) {
    std::cerr << "Filter browsing and preview must leave the canvas undimmed\n"; return 1;
  }
  auto* blur_group = qobject_cast<QQuickItem*>(find("photoEffectGroup_Blur"));
  auto* box_choice = qobject_cast<QQuickItem*>(find("photoEffectChoice_box_blur"));
  auto* gaussian_choice = qobject_cast<QQuickItem*>(choice);
  const auto box_position = box_choice ? box_choice->mapToScene(QPointF()) : QPointF();
  const auto gaussian_position = gaussian_choice ? gaussian_choice->mapToScene(QPointF()) : QPointF();
  const bool box_precedes_gaussian = box_position.y() < gaussian_position.y() - 1.0 ||
      (std::abs(box_position.y() - gaussian_position.y()) <= 1.0 && box_position.x() < gaussian_position.x());
  if (!blur_group || !box_choice || !category_picker ||
      category_model.indexOf(QStringLiteral("Blur")) < 0 ||
      category_model.indexOf(QStringLiteral("Film")) < 0 ||
      category_model.indexOf(QStringLiteral("Blur")) >= category_model.indexOf(QStringLiteral("Film")) ||
      !box_precedes_gaussian) {
    std::cerr << "Filters must be grouped by category and alphabetized within each group; categories="
              << category_model.join(QStringLiteral(",")).toStdString()
              << " blur=" << (blur_group != nullptr) << " box=" << (box_choice != nullptr)
              << " categoryPicker=" << (category_picker != nullptr)
              << " boxX=" << (box_choice ? box_choice->mapToScene(QPointF()).x() : -1)
              << " boxY=" << (box_choice ? box_choice->mapToScene(QPointF()).y() : -1)
              << " gaussianX=" << (gaussian_choice ? gaussian_choice->mapToScene(QPointF()).x() : -1)
              << " gaussianY=" << (gaussian_choice ? gaussian_choice->mapToScene(QPointF()).y() : -1)
              << '\n'; return 1;
  }
  QMetaObject::invokeMethod(category_picker, "activated", Q_ARG(int, 1));
  settle();
  if (find("photoEffectGroup_Film") || find("photoEffectLookChoice_dream_haze")) return 1;
  QMetaObject::invokeMethod(category_picker, "activated", Q_ARG(int, 0));
  settle();
  choice = find("photoEffectChoice_gaussian_blur");
  if (!click(choice)) return 1;
  if (!root->property("draft").toBool() || !picker->property("visible").toBool() ||
      !apply->property("enabled").toBool() || !apply->property("visible").toBool() ||
      !preview_cancel->property("visible").toBool()) return 1;
  auto* transaction_hint = find("photoEffectTransactionHint");
  if (!transaction_hint || !transaction_hint->property("visible").toBool() ||
      !transaction_hint->property("text").toString().contains(QStringLiteral("Apply commits"))) return 1;
  if (picker->property("browsing").toBool() || picker->property("dim").toBool()) return 1;
  QVariant committedSize;
  if (!QMetaObject::invokeMethod(size, "testCommitInputText",
                                 Q_RETURN_ARG(QVariant, committedSize),
                                 Q_ARG(QVariant, QVariant(QStringLiteral("23"))))) return 1;
  if (root->property("sizeEdits").toInt() != 1) return 1;
  if (!capture(QStringLiteral("effects-live-preview.png"))) return 1;
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &escape);
  settle();
  if (root->property("draft").toBool() || root->property("cancelled").toInt() != 1) return 1;
  if (!click(add)) return 1;
  choice = find("photoEffectChoice_gaussian_blur");
  if (!click(choice) || !click(apply)) return 1;
  if (root->property("applied").toInt() != 1 || root->property("cancelled").toInt() != 1 ||
      picker->property("visible").toBool()) return 1;
  if (!click(add)) return 1;
  choice = find("photoEffectChoice_gaussian_blur");
  if (!click(choice)) return 1;
  auto* cancel_button = find("photoEffectPreviewCancel");
  if (!cancel_button || !click(cancel_button) || root->property("cancelled").toInt() != 2) return 1;
  QMetaObject::invokeMethod(add, "clicked");
  settle();
  choice = find("photoEffectChoice_gaussian_blur");
  if (!choice) return 1;
  QMetaObject::invokeMethod(choice, "clicked");
  QMetaObject::invokeMethod(root.get(), "assetChanged");
  settle();
  if (root->property("draft").toBool() || picker->property("visible").toBool()) return 1;
  root->setProperty("reject", true);
  QMetaObject::invokeMethod(add, "clicked");
  settle();
  choice = find("photoEffectChoice_gaussian_blur");
  if (!choice) return 1;
  QMetaObject::invokeMethod(choice, "clicked");
  if (apply->property("enabled").toBool() || picker->property("errorMessage").toString().isEmpty()) return 1;
  auto* search = find("photoEffectSearchField");
  if (!search) return 1;
  search->setProperty("text", QStringLiteral("Optical"));
  settle();
  auto* look = find("photoEffectLookChoice_dream_haze");
  if (!look) return 1;
  if (!click(look)) return 1;
  if (root->property("lookApplications").toInt() != 0 ||
      !picker->property("visible").toBool()) return 1;
  root->setProperty("reject", false);
  look = find("photoEffectLookChoice_dream_haze");
  auto* look_card = qobject_cast<QQuickItem*>(look);
  if (!look_card || look_card->property("badgeText").toString() != QStringLiteral("APPLIES NOW") ||
      look_card->property("thumbnailSource").toString() != root->property("mockLookThumbnailSource").toString() ||
      !look_card->property("previewReady").toBool()) {
    std::cerr << "Looks need a real sample and a distinct immediate-apply label\n"; return 1;
  }
  if (!click(look)) return 1;
  if (root->property("lookApplications").toInt() != 1 ||
      picker->property("visible").toBool()) return 1;
  root->setProperty("hasSavedEffect", true);
  settle();
  auto* bypass = qobject_cast<QQuickItem*>(find("photoEffectEnabled_saved-effect"));
  auto* expand = find("photoEffectExpand_saved-effect");
  auto* amount = qobject_cast<QQuickItem*>(find("photoGaussianAmountField"));
  auto* state = qobject_cast<QQuickItem*>(find("photoEffectState_saved-effect"));
  auto* state_text = find("photoEffectStateText_saved-effect");
  auto* mask = qobject_cast<QQuickItem*>(find("photoEffectMask_saved-effect"));
  if (!bypass || !bypass->isVisible() || !expand ||
      !amount || !amount->isVisible() || !state || !state->isVisible() || !mask || !mask->isVisible() ||
      !state_text || state_text->property("text").toString() != QStringLiteral("Enabled")) {
    std::cerr << "Collapsed effect must expose amount, enabled state, and its mask badge before details\n";
    return 1;
  }
  if (!click(bypass)) return 1;
  bypass = qobject_cast<QQuickItem*>(find("photoEffectEnabled_saved-effect"));
  state_text = find("photoEffectStateText_saved-effect");
  if (root->property("effectEnabled").toBool() || bypass->property("checked").toBool() ||
      root->property("effectAmount").toDouble() != 0.4 || root->property("bypassEdits").toInt() != 1 ||
      !state_text || state_text->property("text").toString() != QStringLiteral("Bypassed")) {
    std::cerr << "Bypass must retain effect settings and update its visible state\n";
    return 1;
  }
  amount = qobject_cast<QQuickItem*>(find("photoGaussianAmountField"));
  if (!amount) return 1;
  auto* amount_control = qobject_cast<QQuickItem*>(find("photoGaussianAmountFieldControl"));
  if (!amount_control || !amount_control->isEnabled()) return 1;
  const auto amount_start = amount_control->mapToScene(
      QPointF(amount_control->width() * 0.45, amount_control->height() / 2)).toPoint();
  const auto amount_end = amount_control->mapToScene(
      QPointF(amount_control->width() * 0.75, amount_control->height() / 2)).toPoint();
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, amount_start);
  QTest::qWait(20);
  QTest::mouseMove(&window, amount_end, 20);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, amount_end);
  settle();
  if (root->property("coalescedBegins").toInt() != 1 ||
      root->property("coalescedEnds").toInt() != 1 ||
      root->property("amountEdits").toInt() < 1 ||
      root->property("lastAmountId").toString() != QStringLiteral("saved-effect")) {
    std::cerr << "Amount pointer editing must use the effect ID and one coalesced edit"
              << " begins=" << root->property("coalescedBegins").toInt()
              << " ends=" << root->property("coalescedEnds").toInt()
              << " edits=" << root->property("amountEdits").toInt() << '\n';
    return 1;
  }
  if (find("photoGaussianAmountFieldControl") != amount_control) {
    std::cerr << "Updating amount must preserve the active effect card delegate\n";
    return 1;
  }
  amount = qobject_cast<QQuickItem*>(find("photoGaussianAmountField"));
  if (!amount) return 1;
  QVariant committed_amount;
  const bool amount_committed = QMetaObject::invokeMethod(amount, "testCommitInputText",
      Q_RETURN_ARG(QVariant, committed_amount), Q_ARG(QVariant, QVariant(QStringLiteral("63.5"))));
  if (!amount_committed ||
      root->property("amountEdits").toInt() < 2 ||
      root->property("lastAmountId").toString() != QStringLiteral("saved-effect") ||
      std::abs(root->property("lastAmount").toDouble() - 0.635) > 1e-8 ||
      root->property("coalescedEnds").toInt() != 1) {
    std::cerr << "Amount input must edit the selected effect by ID inside one coalesced edit"
              << " commit=" << amount_committed
              << " returned=" << committed_amount.toString().toStdString()
              << " edits=" << root->property("amountEdits").toInt()
              << " id=" << root->property("lastAmountId").toString().toStdString()
              << " value=" << root->property("lastAmount").toDouble()
              << " begins=" << root->property("coalescedBegins").toInt()
              << " ends=" << root->property("coalescedEnds").toInt() << '\n';
    return 1;
  }
  QMetaObject::invokeMethod(stack, "refreshEffects");
  settle();
  expand = find("photoEffectExpand_saved-effect");
  if (!expand) return 1;
  if (!click(expand)) return 1;
  if (!capture(QStringLiteral("effects-card-details.png"))) return 1;
  auto* edit_mask = find("photoEffectEditMask_saved-effect");
  if (!edit_mask || !click(edit_mask) ||
      root->property("maskEditRequests").toInt() != 1 ||
      root->property("editedMaskSetId").toString() != QStringLiteral("effect-mask")) {
    std::cerr << "Edit effect mask must route its stable mask identity to the workspace target\n";
    return 1;
  }
  amount = qobject_cast<QQuickItem*>(find("photoGaussianAmountField"));
  auto* radius = qobject_cast<QQuickItem*>(find("photoGaussianSizeField"));
  if (!amount || !radius || !amount->isVisible() ||
      amount->mapToScene(QPointF()).y() >= radius->mapToScene(QPointF()).y()) {
    std::cerr << "Effect amount must be available before advanced parameters"
              << " amount=" << (amount && amount->isVisible())
              << " radius=" << (radius && radius->isVisible())
              << " amountY=" << (amount ? amount->mapToScene(QPointF()).y() : -1)
              << " radiusY=" << (radius ? radius->mapToScene(QPointF()).y() : -1)
              << " expanded=" << stack->property("expandedEffectId").toString().toStdString() << '\n';
    return 1;
  }

  root->setProperty("effectEnabled", true);
  root->setProperty("hasSecondEffect", true);
  QMetaObject::invokeMethod(stack, "refreshEffects");
  settle();
  auto* first_earlier = qobject_cast<QQuickItem*>(find("photoEffectEarlier_saved-effect"));
  auto* first_later = qobject_cast<QQuickItem*>(find("photoEffectLater_saved-effect"));
  auto* second_earlier = qobject_cast<QQuickItem*>(find("photoEffectEarlier_second-effect"));
  auto* second_later = qobject_cast<QQuickItem*>(find("photoEffectLater_second-effect"));
  if (!first_earlier || !first_later || !second_earlier || !second_later ||
      first_earlier->property("enabled").toBool() || !first_later->property("enabled").toBool() ||
      !second_earlier->property("enabled").toBool() || second_later->property("enabled").toBool()) {
    std::cerr << "Reorder controls must be visible and disabled at the correct stack boundaries\n";
    return 1;
  }
  stack->setProperty("locked", true);
  settle();
  if (!capture(QStringLiteral("effects-stack-locked.png"))) return 1;
  bypass = qobject_cast<QQuickItem*>(find("photoEffectEnabled_saved-effect"));
  amount = qobject_cast<QQuickItem*>(find("photoGaussianAmountField"));
  first_later = qobject_cast<QQuickItem*>(find("photoEffectLater_saved-effect"));
  if (!bypass || bypass->property("toggleEnabled").toBool() || !amount || amount->property("enabled").toBool() ||
      !first_later || first_later->property("enabled").toBool()) {
    std::cerr << "Locking a layer must disable bypass, amount, and reorder controls\n";
    return 1;
  }
  stack->setProperty("locked", false);
  if (!click(first_later)) return 1;
  if (root->property("moveCalls").toInt() != 1 || root->property("lastMoveTo").toInt() != 1) {
    std::cerr << "The Later action must use the controller's effect ordering API\n";
    return 1;
  }
  if (!capture(QStringLiteral("effects-stack-multieffect-compact.png"))) return 1;

  auto* density = find("photoEffectDensityToggle");
  const bool compact_before = stack->property("compactCards").toBool();
  if (!density) return 1;
  if (!click(density)) return 1;
  if (stack->property("compactCards").toBool() == compact_before) {
    std::cerr << "Effect density preference must toggle and update the stack\n";
    return 1;
  }
  const auto effect_amount_before_density = root->property("effectAmount").toDouble();
  const auto move_calls_before_density = root->property("moveCalls").toInt();
  if (!capture(stack->property("compactCards").toBool()
          ? QStringLiteral("effects-stack-compact.png")
          : QStringLiteral("effects-stack-comfortable.png"))) return 1;
  auto* effect_preferences = find("photoEffectPreferences");
  if (!effect_preferences || !QMetaObject::invokeMethod(effect_preferences, "sync")) {
    std::cerr << "Effect density preference could not synchronize\n";
    return 1;
  }
  settle();
  QSettings persisted_preferences;
  persisted_preferences.sync();
  QElapsedTimer preference_write_timer;
  preference_write_timer.start();
  while (preference_write_timer.elapsed() < 1800 &&
         persisted_preferences.value(QStringLiteral("PhotoEffectStack/compactCards")).toBool() !=
             stack->property("compactCards").toBool()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::qWait(20);
    persisted_preferences.sync();
  }
  if (!persisted_preferences.contains(QStringLiteral("PhotoEffectStack/compactCards")) ||
      persisted_preferences.value(QStringLiteral("PhotoEffectStack/compactCards")).toBool() !=
          stack->property("compactCards").toBool() ||
      root->property("effectAmount").toDouble() != effect_amount_before_density ||
      root->property("moveCalls").toInt() != move_calls_before_density) {
    std::cerr << "Display density must persist without changing effect values or recipe actions"
              << " settings=" << persisted_preferences.value(QStringLiteral("PhotoEffectStack/compactCards")).toBool()
              << " display=" << stack->property("compactCards").toBool()
              << " amount=" << root->property("effectAmount").toDouble() << '\n';
    return 1;
  }
  std::unique_ptr<QObject> preference_probe(component.create());
  settle();
  auto* persisted_stack = qobject_cast<QQuickItem*>(find_from(preference_probe.get(), "stack"));
  if (!preference_probe || !persisted_stack ||
      persisted_stack->property("compactCards").toBool() != stack->property("compactCards").toBool()) {
    std::cerr << "Effect density preference must reload for the next stack instance\n";
    return 1;
  }
  preference_probe.reset();

  const auto fits_stack_width = [&](QObject* object) {
    auto* item = qobject_cast<QQuickItem*>(object);
    if (!item || !stack) return false;
    const auto rect = item->mapRectToItem(stack, QRectF(0, 0, item->width(), item->height()));
    return rect.left() >= -1 && rect.right() <= stack->width() + 1;
  };
  const auto reveal_in_scroll = [&](QQuickItem* item) {
    if (!item) return false;
    for (int pass = 0; pass < 3; ++pass) {
      for (auto* ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (!ancestor->property("contentY").isValid() ||
            !ancestor->property("contentHeight").isValid()) continue;
        const auto rect = item->mapRectToItem(ancestor, item->boundingRect());
        const auto offset = rect.top() < 0 ? rect.top() - 8
            : rect.bottom() > ancestor->height() ? rect.bottom() - ancestor->height() + 8 : 0;
        const auto maximum = std::max(0.0, ancestor->property("contentHeight").toDouble() - ancestor->height());
        ancestor->setProperty("contentY", std::clamp(ancestor->property("contentY").toDouble() + offset,
                                                       0.0, maximum));
        settle();
      }
    }
    return true;
  };
  for (const double scale : {1.0, 2.0}) {
    engine.rootContext()->setContextProperty("appShell", QVariantMap{{"interfaceScale", scale}});
    settle();
    for (const double width : {260.0, 340.0}) {
      root_item->setWidth(width);
      window.resize(static_cast<int>(width), 820);
      settle();
      amount = qobject_cast<QQuickItem*>(find("photoGaussianAmountField"));
      auto* amount_group = qobject_cast<QQuickItem*>(find("photoGaussianAmountResponsiveGroup_saved-effect"));
      auto* amount_label = qobject_cast<QQuickItem*>(find("photoGaussianAmountFieldLabel"));
      state = qobject_cast<QQuickItem*>(find("photoEffectState_saved-effect"));
      mask = qobject_cast<QQuickItem*>(find("photoEffectMask_saved-effect"));
      bypass = qobject_cast<QQuickItem*>(find("photoEffectEnabled_saved-effect"));
      auto* status_row = qobject_cast<QQuickItem*>(find("photoEffectCardStatusRow_saved-effect"));
      auto* effect_title = qobject_cast<QQuickItem*>(find("photoEffectExpand_saved-effect"));
      auto* effect_title_text = effect_title
          ? qobject_cast<QQuickItem*>(find_from(effect_title, "sharedCommandLabel")) : nullptr;
      first_later = qobject_cast<QQuickItem*>(find("photoEffectLater_saved-effect"));
      if (!amount || !amount_group || !amount_label || !state || !mask || !bypass || !status_row ||
          !effect_title || !effect_title_text || !first_later ||
          !amount->isVisible() || !amount_group->isVisible() || !amount_label->isVisible() ||
          amount_label->property("text").toString() != QStringLiteral("Amount (%)") ||
          amount_label->property("lineCount").toInt() > 2 ||
          !state->isVisible() || !mask->isVisible() || !bypass->isVisible() ||
          bypass->width() <= 0 || bypass->height() <= 0 || stack->width() > width + 1 ||
          !fits_stack_width(amount) || !fits_stack_width(amount_group) || !fits_stack_width(status_row) || !fits_stack_width(state) ||
          !fits_stack_width(mask) || !fits_stack_width(bypass) || !fits_stack_width(effect_title) || !fits_stack_width(first_later)) {
        std::cerr << "Essential effect controls must fit at width=" << width << " scale=" << scale
                  << " root=" << root_item->width() << " stack=" << stack->width()
                  << " amount=" << (amount ? amount->width() : -1)
                  << " status=" << (status_row ? status_row->width() : -1)
                  << " bypassVisible=" << (bypass && bypass->isVisible())
                  << " bypassEnabled=" << (bypass && bypass->isEnabled())
                  << " bypassRect=" << (bypass ? bypass->x() : -1) << ',' << (bypass ? bypass->y() : -1)
                  << ' ' << (bypass ? bypass->width() : -1) << 'x' << (bypass ? bypass->height() : -1)
                  << " title=" << (effect_title ? effect_title->width() : -1) << ','
                  << (effect_title ? effect_title->height() : -1) << '\n';
        return 1;
      }
      if (!reveal_in_scroll(amount_group)) return 1;
      auto* panel_scroll = qobject_cast<QQuickItem*>(find("creativeToolsScroll"));
      const auto amount_group_in_scroll = panel_scroll
          ? amount_group->mapRectToItem(panel_scroll, amount_group->boundingRect()) : QRectF();
      if (!panel_scroll || !QRectF(0, 0, panel_scroll->width(), panel_scroll->height())
              .contains(amount_group_in_scroll)) {
        std::cerr << "Amount control must be fully presented in the scroll viewport at width=" << width
                  << " scale=" << scale << '\n';
        return 1;
      }
      if (scale == 2.0 && amount_label->property("lineCount").toInt() != 1) {
        std::cerr << "The 2x Amount label must stay on one readable line at width=" << width
                  << " text=" << amount_label->property("text").toString().toStdString()
                  << " lines=" << amount_label->property("lineCount").toInt()
                  << " width=" << amount_label->width() << '\n';
        return 1;
      }
      const auto expected_effect_title = scale == 2.0 && width == 260.0
          ? QStringLiteral("Gaussian\nblur") : QStringLiteral("Gaussian blur");
      const auto expected_effect_title_lines = expected_effect_title.contains(QLatin1Char('\n')) ? 2 : 1;
      if (scale == 2.0 && (effect_title_text->property("text").toString() != expected_effect_title ||
              effect_title_text->property("lineCount").toInt() != expected_effect_title_lines)) {
        std::cerr << "The 2x effect title must wrap at the word boundary at width=" << width
                  << " text=" << effect_title_text->property("text").toString().toStdString()
                  << " lines=" << effect_title_text->property("lineCount").toInt() << '\n';
        return 1;
      }
      if (scale == 2.0 && width == 260.0) {
        if (!click(bypass)) return 1;
        state_text = find("photoEffectStateText_saved-effect");
        if (root->property("effectEnabled").toBool() || !state_text ||
            state_text->property("text").toString() != QStringLiteral("Bypassed")) {
          std::cerr << "The 2x narrow enabled toggle must respond to a pointer click and update its badge\n";
          return 1;
        }
        if (!capture(QStringLiteral("effects-width-260-scale-2-bypassed.png"))) return 1;
        bypass = qobject_cast<QQuickItem*>(find("photoEffectEnabled_saved-effect"));
        if (!bypass || !click(bypass) || !root->property("effectEnabled").toBool()) {
          std::cerr << "The 2x narrow enabled toggle must restore from bypass by pointer\n";
          return 1;
        }
        if (!reveal_in_scroll(amount_group)) return 1;
      }
      const auto screenshot = QStringLiteral("effects-width-%1-scale-%2.png")
          .arg(static_cast<int>(width)).arg(static_cast<int>(scale));
      if (!capture(screenshot)) return 1;
    }
  }
  return 0;
}
