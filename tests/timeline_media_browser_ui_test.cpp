#include <QCoreApplication>
#include <QByteArray>
#include <QColor>
#include <QAccessible>
#include <QAbstractItemModel>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QQmlPropertyMap>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QPointer>
#include <QSignalSpy>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QString>
#include <QtTest/QTest>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void settle(int milliseconds = 180) {
  const int steps = std::max(1, milliseconds / 5);
  for (int iteration = 0; iteration < steps; ++iteration) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QObject* find_object(QObject* root, const QString& object_name) {
  return root == nullptr ? nullptr : root->findChild<QObject*>(object_name);
}

bool invoke_no_arguments(QObject* object, const char* method) {
  return object != nullptr && QMetaObject::invokeMethod(object, method, Qt::DirectConnection);
}

QQuickItem* find_visual_item(QQuickItem* root, const QString& object_name) {
  if (root == nullptr)
    return nullptr;
  if (root->objectName() == object_name)
    return root;
  for (auto* child : root->childItems()) {
    if (auto* match = find_visual_item(child, object_name))
      return match;
  }
  return nullptr;
}

QQuickItem* find_media_card(QQuickItem* root, const QString& asset_id) {
  if (root == nullptr)
    return nullptr;
  if (root->objectName() == QStringLiteral("timelineMediaAssetCard") &&
      root->property("assetId").toString() == asset_id &&
      root->property("visible").toBool())
    return root;
  for (auto* child : root->childItems()) {
    if (auto* match = find_media_card(child, asset_id))
      return match;
  }
  return nullptr;
}

QQuickItem* find_media_row(QQuickItem* root, const QString& asset_id) {
  if (root == nullptr)
    return nullptr;
  if (root->objectName() == QStringLiteral("timelineMediaAssetRow") &&
      root->property("assetId").toString() == asset_id &&
      root->property("visible").toBool())
    return root;
  for (auto* child : root->childItems()) {
    if (auto* match = find_media_row(child, asset_id))
      return match;
  }
  return nullptr;
}

QPoint pointer_point(QQuickItem* item, const QPointF& relative_position) {
  return item->mapToScene(relative_position).toPoint();
}

bool scroll_item_into_view(QQuickItem* view, QQuickItem* item) {
  if (view == nullptr || item == nullptr || view->height() <= 0.0)
    return false;
  const auto item_rect_in_view = [&]() {
    const QPointF origin = item->mapToItem(view, QPointF(0.0, 0.0));
    return QRectF(origin, QSizeF(item->width(), item->height()));
  };
  QRectF rect = item_rect_in_view();
  if (rect.top() < 0.0) {
    view->setProperty("contentY", view->property("contentY").toDouble() + rect.top());
  } else if (rect.bottom() > view->height()) {
    view->setProperty("contentY", view->property("contentY").toDouble() +
                                  rect.bottom() - view->height());
  }
  settle(80);
  rect = item_rect_in_view();
  const QRectF viewport(0.0, 0.0, view->width(), view->height());
  const QRectF window_bounds(0.0, 0.0, view->window()->width(), view->window()->height());
  const QPointF center_in_view = item->mapToItem(view, QPointF(item->width() / 2.0,
                                                               item->height() / 2.0));
  const QPointF center_in_window = item->mapToScene(QPointF(item->width() / 2.0,
                                                           item->height() / 2.0));
  return rect.intersects(viewport) && viewport.contains(center_in_view) &&
         window_bounds.contains(center_in_window);
}

void click_item(QQuickWindow* window, QQuickItem* item, const QPointF& relative_position,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QTest::mouseClick(window, Qt::LeftButton, modifiers,
                    pointer_point(item, relative_position));
}

QStringList selected_asset_ids(QObject* panel) {
  const QVariant selected = panel == nullptr ? QVariant() : panel->property("selectedMediaAssetIds");
  QStringList ids = selected.toStringList();
  if (!ids.isEmpty())
    return ids;
  for (const QVariant& asset_id : selected.toList())
    ids.push_back(asset_id.toString());
  return ids;
}

bool capture(QQuickWindow* window, const QString& evidence_directory,
             const QString& file_name) {
  if (evidence_directory.isEmpty())
    return true;
  if (!QDir().mkpath(evidence_directory))
    return false;
  const QImage image = window->grabWindow();
  return !image.isNull() && image.save(QDir(evidence_directory).filePath(file_name));
}

QAbstractItemModel* find_media_model_with_asset_ids(QObject* root,
                                                    const QStringList& expected_ids) {
  if (root == nullptr)
    return nullptr;
  if (auto* model = qobject_cast<QAbstractItemModel*>(root)) {
    const auto roles = model->roleNames();
    const auto asset_role = roles.key(QByteArrayLiteral("assetId"), -1);
    if (asset_role >= 0 && model->rowCount() == expected_ids.size()) {
      QSet<QString> actual_ids;
      for (int row = 0; row < model->rowCount(); ++row)
        actual_ids.insert(model->data(model->index(row, 0), asset_role).toString());
      const QSet<QString> expected(expected_ids.cbegin(), expected_ids.cend());
      if (actual_ids == expected)
        return model;
    }
  }
  for (QObject* child : root->children()) {
    if (auto* model = find_media_model_with_asset_ids(child, expected_ids))
      return model;
  }
  return nullptr;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QTemporaryDir thumbnail_directory;
  if (!thumbnail_directory.isValid())
    return fail("A temporary media-thumbnail fixture directory should be available");
  const auto create_thumbnail = [&](const QString& filename, const QColor& base,
                                    const bool waveform) {
    QImage image(480, 270, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.fillRect(image.rect(), base);
    for (int band = 0; band < 8; ++band) {
      const int x = band * image.width() / 8;
      QColor tint = base.lighter(125 + band * 5);
      painter.fillRect(x, 0, image.width() / 8, image.height(), tint);
    }
    if (waveform) {
      QPen waveform_pen(QColor(QStringLiteral("#e2c17b")));
      waveform_pen.setWidth(5);
      painter.setPen(waveform_pen);
      for (int x = 12; x < image.width() - 12; x += 9) {
        const int height = 12 + ((x * 19) % 94);
        painter.drawLine(x, image.height() / 2 - height / 2,
                         x, image.height() / 2 + height / 2);
      }
    } else {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(250, 239, 216, 210));
      painter.drawEllipse(QPoint(330, 112), 62, 62);
      painter.setBrush(QColor(18, 32, 43, 205));
      painter.drawRoundedRect(QRect(72, 146, 198, 92), 18, 18);
      painter.setBrush(QColor(241, 183, 112, 200));
      painter.drawEllipse(QPoint(138, 116), 32, 32);
    }
    painter.end();
    const QString path = thumbnail_directory.filePath(filename);
    if (!image.save(path, "PNG"))
      return QString();
    return QUrl::fromLocalFile(path).toString();
  };
  const QString photo_thumbnail = create_thumbnail(QStringLiteral("portrait.png"),
                                                     QColor(QStringLiteral("#466c73")), false);
  const QString video_thumbnail = create_thumbnail(QStringLiteral("interview.png"),
                                                     QColor(QStringLiteral("#395977")), false);
  const QString audio_thumbnail = create_thumbnail(QStringLiteral("room-tone.png"),
                                                     QColor(QStringLiteral("#3b394f")), true);
  const QString offline_thumbnail = create_thumbnail(QStringLiteral("offline.png"),
                                                       QColor(QStringLiteral("#6f4f4c")), false);
  if (photo_thumbnail.isEmpty() || video_thumbnail.isEmpty() || audio_thumbnail.isEmpty() ||
      offline_thumbnail.isEmpty())
    return fail("The media browser should use generated thumbnail artwork in runtime evidence");
  const QString project_photo_path = thumbnail_directory.filePath(QStringLiteral("project-only-photo.png"));
  const QString project_video_path = thumbnail_directory.filePath(QStringLiteral("project-only-video.mp4"));
  const QString catalog_photo_path = thumbnail_directory.filePath(QStringLiteral("library-catalog-photo.png"));
  QImage project_photo(24, 16, QImage::Format_RGBA8888);
  project_photo.fill(QColor(QStringLiteral("#bd784b")));
  QImage catalog_photo(24, 16, QImage::Format_RGBA8888);
  catalog_photo.fill(QColor(QStringLiteral("#497e79")));
  QFile project_video_file(project_video_path);
  if (!project_photo.save(project_photo_path) || !catalog_photo.save(catalog_photo_path) ||
      !project_video_file.open(QIODevice::WriteOnly) ||
      project_video_file.write(QByteArray("project source fixture\n")) <= 0)
    return fail("Project-only media source paths should exist for Source and timeline routing checks");
  project_video_file.close();
  QQmlPropertyMap app_shell;
  app_shell.insert(QStringLiteral("interfaceScale"), 1.0);
  app_shell.insert(QStringLiteral("interfaceDensity"), QStringLiteral("balanced"));
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &app_shell);
  QQmlComponent component(&engine);
  QString qml = QStringLiteral(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml") + QStringLiteral(PHOTO_EDITOR_SOURCE_DIR) + QStringLiteral(R"qml(/apps/desktop/qml/components"

ApplicationWindow {
    id: fixture
    objectName: "timelineMediaBrowserFixture"
    width: 520
    height: 620
    visible: true
    color: "#17191c"

    QtObject {
        id: fakeCatalog
        objectName: "timelineMediaBrowserFakeCatalog"
        property var assets: [
            { "assetId": "asset-photo", "mediaType": "photo", "filePath": "/media/portrait.jpg",
              "cameraLabel": "Canon EOS 80D", "thumbnailSource": "__PHOTO_THUMBNAIL__", "durationText": "", "durationSeconds": 0,
              "online": true, "captureDate": "2026-10-18", "lensModel": "50mm", "projectNames": [] },
            { "assetId": "asset-video", "mediaType": "video", "filePath": "/media/interview-wide-camera.mov",
              "cameraLabel": "Canon EOS 80D", "thumbnailSource": "__VIDEO_THUMBNAIL__", "durationText": "01:14",
              "durationSeconds": 74, "online": true, "captureDate": "2026-10-19", "lensModel": "24-70mm",
              "projectNames": [] },
            { "assetId": "asset-audio", "mediaType": "audio", "filePath": "/media/room-tone.wav",
              "cameraLabel": "Room tone", "thumbnailSource": "__AUDIO_THUMBNAIL__", "durationText": "00:32",
              "durationSeconds": 32, "online": true, "captureDate": "2026-10-19", "lensModel": "",
              "projectNames": [] },
            { "assetId": "asset-offline", "mediaType": "photo", "filePath": "/media/offline-select.jpg",
              "cameraLabel": "Canon EOS 80D", "thumbnailSource": "__OFFLINE_THUMBNAIL__", "durationText": "", "durationSeconds": 0,
              "online": false, "captureDate": "", "lensModel": "", "projectNames": [] }
        ]
        property int assetCount: assets.length
        property int totalAssetCount: assets.length
        property bool loading: false
        property int lastVisibleFirst: -2
        property int lastVisibleLast: -2
        property int visibleRangeCallCount: 0

        function assetAt(index) { return index >= 0 && index < assets.length ? assets[index] : ({}) }
        function assetById(assetId) {
            for (let index = 0; index < assets.length; ++index) {
                if (String(assets[index].assetId) === String(assetId))
                    return assets[index]
            }
            return ({})
        }
        function setVisibleAssetRange(firstIndex, lastIndex) {
            lastVisibleFirst = firstIndex
            lastVisibleLast = lastIndex
            visibleRangeCallCount += 1
        }
    }

    QtObject {
        id: fakeTimelineDocument
        objectName: "timelineMediaBrowserFakeDocument"
        property bool hasSourceClip: false
        property bool sourceTapeEnabled: false
        property bool sourcePatchReady: false
        property bool hasOfflineMediaItems: false
        property string activeMediaBinId: ""
        property string activeSmartCollectionId: ""
        property var mediaBins: []
        property var smartCollections: []
        property var sourceTapeItems: []
        property int sourceLoadCount: 0
        property int insertCount: 0
        property int overwriteCount: 0
        property int metadataMutationCount: 0
        property string lastLoadedAssetId: ""
        property string lastLoadedFilePath: ""
        property string lastInsertedAssetId: ""
        property string lastInsertedFilePath: ""
        property string lastOverwrittenAssetId: ""
        property var usedMediaRows: []
        property int usedMediaSnapshotCallCount: 0
        signal clipsChanged()
        signal sequenceChanged()
        signal timelineStateChanged()
        signal mediaHoverFrameReady(string assetId, real sourceSeconds, string frameSource)
        function loadSourceClipFromGallery(assetId, filePath, displayLabel, offset, thumbnail) {
            sourceLoadCount += 1
            lastLoadedAssetId = assetId
            lastLoadedFilePath = filePath
            return true
        }
        function insertClipFromGallery(assetId, filePath, displayLabel, duration, trimIn, trimOut, thumbnail) {
            insertCount += 1
            lastInsertedAssetId = assetId
            lastInsertedFilePath = filePath
            return true
        }
        function overwriteClipFromGallery(assetId, filePath, displayLabel, duration, trimIn, trimOut, thumbnail) {
            overwriteCount += 1
            lastOverwrittenAssetId = assetId
            return true
        }
        function mediaAssetVisibleInBin(assetId, binId) { return true }
        function mediaAssetMatchesSmartCollection(asset, collectionId) { return true }
        function mediaAssetMetadata(assetId) { return ({}) }
        function searchMediaAsset(assetId, query, scopes, asset) {
            const filename = String(asset.filePath || "").split(/[\\/]/).pop().toLocaleLowerCase()
            const matches = filename.indexOf(String(query || "").toLocaleLowerCase()) >= 0
            return { "matches": matches, "matchSummary": matches ? "Filename" : "" }
        }
        function setMediaAssetRating(assetIds, rating) { metadataMutationCount += 1; return true }
        function setMediaAssetDisposition(assetIds, disposition) { metadataMutationCount += 1; return true }
        function requestMediaHoverFrame(assetId, filePath, sourceSeconds) { return "" }
        function snapshotTimelineUsedMedia() {
            usedMediaSnapshotCallCount += 1
            return usedMediaRows
        }
    }

    Theme { id: sharedTheme }

    TimelineSourcePanel {
        id: sourcePanel
        objectName: "timelineMediaBrowserSourcePanel"
        anchors.fill: parent
        theme: sharedTheme
        timelineDocument: fakeTimelineDocument
        sourceClip: ({})
        catalogAssets: fakeCatalog
        collapsed: false
        activeTab: "media"
        onTabChangeRequested: function(tab) {
            if (tab === "source")
                fixture.sourceTabRequests += 1
        }
        onMediaDragMoved: fixture.mediaDragMoveCount += 1
        onMediaDragDropped: function(payload, sceneX, sceneY) {
            fixture.mediaDragDropCount += 1
            fixture.lastDroppedAssetId = String(payload.assetId || "")
            fixture.lastDroppedFilePath = String(payload.filePath || "")
            fixture.lastDroppedSceneX = sceneX
            fixture.lastDroppedSceneY = sceneY
        }
    }

    property int sourceTabRequests: 0
    property int mediaDragMoveCount: 0
    property int mediaDragDropCount: 0
    property string lastDroppedAssetId: ""
    property string lastDroppedFilePath: ""
    property real lastDroppedSceneX: -1
    property real lastDroppedSceneY: -1
}
)qml");
  qml.replace(QStringLiteral("__PHOTO_THUMBNAIL__"), photo_thumbnail);
  qml.replace(QStringLiteral("__VIDEO_THUMBNAIL__"), video_thumbnail);
  qml.replace(QStringLiteral("__AUDIO_THUMBNAIL__"), audio_thumbnail);
  qml.replace(QStringLiteral("__OFFLINE_THUMBNAIL__"), offline_thumbnail);
  component.setData(qml.toUtf8(), QUrl::fromLocalFile(
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/timeline_media_browser_ui_test.qml")));

  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Timeline media browser fixture should instantiate");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  QQuickItem* visual_root = window != nullptr ? window->contentItem() : nullptr;
  QObject* panel = find_object(fixture.get(), QStringLiteral("timelineMediaBrowserSourcePanel"));
  QObject* catalog = find_object(fixture.get(), QStringLiteral("timelineMediaBrowserFakeCatalog"));
  QObject* document = find_object(fixture.get(), QStringLiteral("timelineMediaBrowserFakeDocument"));
  if (window == nullptr || visual_root == nullptr || panel == nullptr || catalog == nullptr || document == nullptr)
    return fail("Timeline media browser, window, fake catalog, and fake timeline document should be discoverable");

  const QString evidence_directory = qEnvironmentVariable("LOOKSY_TIMELINE_MEDIA_UI_EVIDENCE_DIR");
  settle();
  if (panel->property("mediaViewMode").toString() != QStringLiteral("grid") ||
      panel->property("mediaResultCountValue").toInt() != 4)
    return fail("The default media browser should show all four items in grid mode");

  QObject* grid = find_visual_item(visual_root, QStringLiteral("timelineVirtualizedMediaGrid"));
  QObject* slider = find_visual_item(visual_root, QStringLiteral("timelineMediaThumbnailSizeSlider"));
  QObject* initial_no_results = find_visual_item(visual_root, QStringLiteral("timelineMediaNoResultsState"));
  if (grid == nullptr || slider == nullptr || grid->property("count").toInt() != 4 ||
      slider->property("visible").toBool() != true || initial_no_results == nullptr)
    return fail("The default grid should virtualize all media and expose its thumbnail-size slider");
  for (const auto& asset_id : {QStringLiteral("asset-photo"), QStringLiteral("asset-video"),
                               QStringLiteral("asset-audio"), QStringLiteral("asset-offline")}) {
    QQuickItem* card = find_media_card(visual_root, asset_id);
    if (card == nullptr)
      return fail("The initial grid should render photo, video, audio, and offline media cards");
    QQuickItem* image = find_visual_item(card, QStringLiteral("timelineMediaCardImage"));
    if (image == nullptr || !QTest::qWaitFor([&] {
          return image->property("status").toInt() == 1;
        }, 3000))
      return fail("Each media type should render its generated thumbnail art in the grid");
    QObject* metadata = find_visual_item(card, QStringLiteral("timelineMediaCardMetadata"));
    if (asset_id != QStringLiteral("asset-offline") && metadata != nullptr &&
        metadata->property("visible").toBool())
      return fail("Capture date and lens metadata should stay out of unselected grid cards");
  }
  if (find_visual_item(visual_root, QStringLiteral("timelineMediaSelectionHint")) != nullptr)
    return fail("The populated grid should not reserve space for persistent usage instructions");
  QQuickItem* repeated_label_video_card = find_media_card(visual_root, QStringLiteral("asset-video"));
  QQuickItem* repeated_label_filename = find_visual_item(
      repeated_label_video_card, QStringLiteral("timelineMediaCardFilename"));
  if (repeated_label_filename == nullptr ||
      repeated_label_filename->property("text").toString() !=
          QStringLiteral("interview-wide-camera.mov"))
    return fail("Grid filenames should identify distinct source files when camera labels repeat");
  auto* video_accessible = QAccessible::queryAccessibleInterface(repeated_label_video_card);
  if (video_accessible == nullptr ||
      video_accessible->text(QAccessible::Name) != QStringLiteral("interview-wide-camera.mov") ||
      !video_accessible->text(QAccessible::Description).contains(
          QStringLiteral("/media/interview-wide-camera.mov")))
    return fail("A grid card should expose its source filename and location to accessibility services");
  if (!capture(window, evidence_directory, QStringLiteral("timeline-media-grid.png")))
    return fail("Could not capture the default grid evidence image");

  window->setWidth(320);
  settle(100);
  if (!capture(window, evidence_directory, QStringLiteral("timeline-media-grid-320px-scale-100.png")))
    return fail("Could not capture the narrow 320 pixel grid evidence image");
  window->setWidth(360);
  settle(100);
  if (!capture(window, evidence_directory, QStringLiteral("timeline-media-grid-360px-scale-100.png")))
    return fail("Could not capture the 360 pixel grid evidence image");
  window->setWidth(320);
  settle(100);
  app_shell.insert(QStringLiteral("interfaceScale"), 1.25);
  settle(100);
  if (!capture(window, evidence_directory, QStringLiteral("timeline-media-grid-320px-scale-125.png")))
    return fail("Could not capture the scaled narrow grid evidence image");
  window->setWidth(520);
  app_shell.insert(QStringLiteral("interfaceScale"), 1.0);
  settle(100);

  // Values that keep the same column count still need visibly different tiles.
  panel->setProperty("mediaThumbnailTargetSize", 144);
  settle(60);
  const auto first_cell = grid->property("cellWidth").toDouble();
  panel->setProperty("mediaThumbnailTargetSize", 160);
  settle(60);
  if (grid->property("cellWidth").toDouble() <= first_cell + 10)
    return fail("Small thumbnail-size changes must resize cells without waiting for a column-count threshold");
  auto* slider_item = qobject_cast<QQuickItem*>(slider);
  if (slider_item == nullptr)
    return fail("The thumbnail-size slider should be an interactive QQuickItem");
  click_item(window, slider_item, QPointF(slider_item->width() - 3, slider_item->height() / 2));
  settle(60);
  if (panel->property("mediaThumbnailTargetSize").toInt() < 240 ||
      grid->property("cellWidth").toInt() < 240)
    return fail("Changing the thumbnail-size slider should resize the actual grid cells");

  QQuickItem* video_card = find_media_card(visual_root, QStringLiteral("asset-video"));
  if (video_card == nullptr)
    return fail("The virtualized grid should create a visible video card delegate");
  auto* grid_item = qobject_cast<QQuickItem*>(grid);
  if (!scroll_item_into_view(grid_item, video_card))
    return fail("A grid card should be brought into the visible viewport before pointer selection");
  QQuickItem* initial_video_preview = find_visual_item(
      video_card, QStringLiteral("timelineMediaCardPreview"));
  if (initial_video_preview == nullptr)
    return fail("A video grid card should expose its hover-scrub preview");
  QTest::mouseMove(window, pointer_point(initial_video_preview,
      QPointF(initial_video_preview->width() * 0.76, initial_video_preview->height() / 2)), 40);
  settle(120);
  if (video_card->property("pendingHoverSeconds").toDouble() < 50.0)
    return fail("Hovering over a video thumbnail should scrub to the matching source time");
  click_item(window, video_card,
             QPointF(video_card->width() / 2, video_card->height() / 2));
  settle(40);
  if (selected_asset_ids(panel) != QStringList{QStringLiteral("asset-video")})
    return fail("A pointer click on a grid card should select its asset ID");

  QObject* insert_button = find_visual_item(visual_root, QStringLiteral("timelineMediaSelectedAddToTimelineButton"));
  auto* insert_button_item = qobject_cast<QQuickItem*>(insert_button);
  if (insert_button_item == nullptr || !insert_button->property("visible").toBool())
    return fail("Selecting a grid card should expose the contextual Add to Timeline action");
  click_item(window, insert_button_item,
             QPointF(insert_button_item->width() / 2, insert_button_item->height() / 2));
  settle(50);
  if (document->property("insertCount").toInt() != 1 ||
      document->property("lastInsertedAssetId").toString() != QStringLiteral("asset-video"))
    return fail("The pointer-driven Add to Timeline action should route the selected asset to the controller");

  video_card = find_media_card(visual_root, QStringLiteral("asset-video"));
  if (!scroll_item_into_view(grid_item, video_card))
    return fail("A selected grid card should remain in the visible viewport for double-click preview");
  const QPoint double_click_point = pointer_point(
      video_card, QPointF(video_card->width() / 2, video_card->height() / 2));
  QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, double_click_point);
  settle(70);
  if (document->property("sourceLoadCount").toInt() < 1 ||
      document->property("lastLoadedAssetId").toString() != QStringLiteral("asset-video") ||
      fixture->property("sourceTabRequests").toInt() < 1)
    return fail("A grid card double-click should load the source clip and switch to the Source tab");

  video_card = find_media_card(visual_root, QStringLiteral("asset-video"));
  if (!scroll_item_into_view(grid_item, video_card))
    return fail("The selected grid preview should remain visible for drag-and-drop interaction");
  QQuickItem* card_preview = find_visual_item(video_card, QStringLiteral("timelineMediaCardPreview"));
  if (card_preview == nullptr)
    return fail("The video card should expose its draggable preview area");
  const QPoint drag_start = pointer_point(
      card_preview, QPointF(card_preview->width() / 2, card_preview->height() / 2));
  const QPoint drag_end = drag_start + QPoint(30, 3);
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, drag_start);
  QTest::mouseMove(window, drag_end, 50);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, drag_end);
  settle(60);
  if (fixture->property("mediaDragMoveCount").toInt() < 1 ||
      fixture->property("mediaDragDropCount").toInt() < 1 ||
      fixture->property("lastDroppedAssetId").toString() != QStringLiteral("asset-video") ||
      fixture->property("lastDroppedFilePath").toString() !=
          QStringLiteral("/media/interview-wide-camera.mov") ||
      std::abs(fixture->property("lastDroppedSceneX").toDouble() - drag_end.x()) > 2.0 ||
      std::abs(fixture->property("lastDroppedSceneY").toDouble() - drag_end.y()) > 2.0)
    return fail("Dragging a grid preview should route its media payload to the actual pointer destination");

  QObject* card_actions = find_visual_item(
      video_card, QStringLiteral("timelineMediaAssetCardActionsMenuPrimaryAction"));
  auto* card_actions_item = qobject_cast<QQuickItem*>(card_actions);
  if (card_actions_item == nullptr)
    return fail("The grid card should expose its explicit media actions menu");
  click_item(window, card_actions_item,
             QPointF(card_actions_item->width() / 2, card_actions_item->height() / 2));
  settle(60);
  QObject* overwrite_menu_item = find_visual_item(
      visual_root, QStringLiteral("timelineMediaAssetCardActionsMenuPopupItem2"));
  auto* overwrite_menu_item_quick = qobject_cast<QQuickItem*>(overwrite_menu_item);
  if (overwrite_menu_item_quick == nullptr || !overwrite_menu_item->property("visible").toBool())
    return fail("The media menu should expose the explicit Overwrite at Playhead action");
  click_item(window, overwrite_menu_item_quick,
             QPointF(overwrite_menu_item_quick->width() / 2, overwrite_menu_item_quick->height() / 2));
  settle(60);
  if (document->property("overwriteCount").toInt() != 1 ||
      document->property("lastOverwrittenAssetId").toString() != QStringLiteral("asset-video"))
    return fail("The explicit Overwrite action should route the selected source through the controller");

  video_card = find_media_card(visual_root, QStringLiteral("asset-video"));
  video_card->forceActiveFocus(Qt::OtherFocusReason);
  QTest::keyClick(window, Qt::Key_Return);
  settle(60);
  if (document->property("sourceLoadCount").toInt() < 2)
    return fail("Return on a focused grid card should preview it in Source");

  QQuickItem* audio_card = find_media_card(visual_root, QStringLiteral("asset-audio"));
  if (audio_card == nullptr || !scroll_item_into_view(grid_item, audio_card))
    return fail("A second media card should scroll into view for plain-click replacement");
  click_item(window, audio_card, QPointF(audio_card->width() / 2, audio_card->height() / 2));
  settle(50);
  QStringList media_selection = selected_asset_ids(panel);
  if (media_selection != QStringList{QStringLiteral("asset-audio")})
    return fail("An unmodified media click should replace the existing selection with one asset");
  insert_button = find_visual_item(visual_root, QStringLiteral("timelineMediaSelectedAddToTimelineButton"));
  if (insert_button == nullptr || !insert_button->property("visible").toBool())
    return fail("A plain click should keep the single-selection timeline action available");

  video_card = find_media_card(visual_root, QStringLiteral("asset-video"));
  if (video_card == nullptr || !scroll_item_into_view(grid_item, video_card))
    return fail("The video card should scroll into view for modifier-based multi-selection");
  click_item(window, video_card, QPointF(video_card->width() / 2, video_card->height() / 2),
             Qt::ControlModifier);
  settle(50);
  media_selection = selected_asset_ids(panel);
  QObject* multi_selection_hint = find_visual_item(
      visual_root, QStringLiteral("timelineMediaMultiSelectionActionHint"));
  if (media_selection.size() != 2 || !media_selection.contains(QStringLiteral("asset-video")) ||
      !media_selection.contains(QStringLiteral("asset-audio")) || multi_selection_hint == nullptr ||
      !multi_selection_hint->property("visible").toBool())
    return fail("Control-click should toggle a second media item into the selection");
  click_item(window, video_card, QPointF(video_card->width() / 2, video_card->height() / 2),
             Qt::ControlModifier);
  settle(40);
  if (selected_asset_ids(panel) != QStringList{QStringLiteral("asset-audio")})
    return fail("Control-clicking a selected asset again should remove just that asset");
  click_item(window, video_card, QPointF(video_card->width() / 2, video_card->height() / 2),
             Qt::ControlModifier);
  settle(40);
  media_selection = selected_asset_ids(panel);
  if (media_selection.size() != 2 || !media_selection.contains(QStringLiteral("asset-video")) ||
      !media_selection.contains(QStringLiteral("asset-audio")))
    return fail("Control-click should restore the removed asset without replacing the other selection");

  panel->setProperty("mediaSearchText", QStringLiteral("INTERVIEW"));
  settle(220);
  if (panel->property("mediaResultCountValue").toInt() != 1 ||
      initial_no_results->property("visible").toBool())
    return fail("Media search should return only the matching filename without a false empty state");
  if (catalog->property("lastVisibleFirst").toInt() != 1 ||
      catalog->property("lastVisibleLast").toInt() != 1)
    return fail("Visible thumbnail scheduling should map filtered results back to catalog index 1");

  panel->setProperty("mediaSearchText", QStringLiteral("e"));
  settle(220);
  if (panel->property("mediaResultCountValue").toInt() != 3)
    return fail("The filtered range-selection fixture should contain video, audio, and offline media");
  grid = find_visual_item(visual_root, QStringLiteral("timelineVirtualizedMediaGrid"));
  grid_item = qobject_cast<QQuickItem*>(grid);
  if (grid_item == nullptr)
    return fail("Filtering should restore the active virtualized grid before range selection");
  video_card = find_media_card(visual_root, QStringLiteral("asset-video"));
  if (video_card == nullptr || !scroll_item_into_view(grid_item, video_card))
    return fail("The first filtered result should be visible to establish the range anchor");
  click_item(window, video_card, QPointF(video_card->width() / 2, video_card->height() / 2));
  settle(40);
  QQuickItem* offline_card = find_media_card(visual_root, QStringLiteral("asset-offline"));
  if (offline_card == nullptr || !scroll_item_into_view(grid_item, offline_card))
    return fail("The last filtered result should scroll into view for Shift range selection");
  click_item(window, offline_card, QPointF(offline_card->width() / 2, offline_card->height() / 2),
             Qt::ShiftModifier);
  settle(50);
  media_selection = selected_asset_ids(panel);
  if (media_selection.size() != 3 || !media_selection.contains(QStringLiteral("asset-video")) ||
      !media_selection.contains(QStringLiteral("asset-audio")) ||
      !media_selection.contains(QStringLiteral("asset-offline")) ||
      media_selection.contains(QStringLiteral("asset-photo")))
    return fail("Shift-click should select the contiguous range in the filtered media results");

  panel->setProperty("mediaSearchText", QStringLiteral("nothing-matches-this"));
  settle(220);
  QObject* no_results = find_visual_item(visual_root, QStringLiteral("timelineMediaNoResultsState"));
  QObject* reset_button = find_visual_item(visual_root, QStringLiteral("timelineResetMediaFiltersButton"));
  QObject* hidden_selection_label = find_visual_item(
      visual_root, QStringLiteral("timelineSelectedMediaContextLabel"));
  const QStringList expected_hidden_selection{QStringLiteral("asset-video"),
                                               QStringLiteral("asset-audio"),
                                               QStringLiteral("asset-offline")};
  if (no_results == nullptr || reset_button == nullptr ||
      !no_results->property("visible").toBool() || !reset_button->property("enabled").toBool() ||
      selected_asset_ids(panel).size() != expected_hidden_selection.size() ||
      !std::all_of(expected_hidden_selection.cbegin(), expected_hidden_selection.cend(),
                   [&](const QString& id) { return selected_asset_ids(panel).contains(id); }) ||
      panel->property("selectedMediaVisibleAssetCount").toInt() != 0 ||
      panel->property("selectedMediaHiddenAssetCount").toInt() != expected_hidden_selection.size() ||
      hidden_selection_label == nullptr ||
      !hidden_selection_label->property("text").toString().contains(QStringLiteral("3 selected")) ||
      !hidden_selection_label->property("text").toString().contains(QStringLiteral("3 hidden")))
    return fail("A real zero-match search should expose its empty state and enabled reset action");
  if (!capture(window, evidence_directory, QStringLiteral("timeline-media-no-results.png")))
    return fail("Could not capture the no-results evidence image");
  if (!invoke_no_arguments(reset_button, "clicked"))
    return fail("The no-results reset button should be clickable");
  settle(220);
  if (panel->property("mediaSearchText").toString().size() != 0 ||
      panel->property("mediaResultCountValue").toInt() != 4)
    return fail("Reset should clear the search and restore the full media result set");

  QObject* list_button = find_visual_item(visual_root, QStringLiteral("timelineMediaListViewButton"));
  if (!invoke_no_arguments(list_button, "clicked"))
    return fail("The list view toggle should be clickable");
  settle(100);
  QObject* list = find_visual_item(visual_root, QStringLiteral("timelineVirtualizedMediaList"));
  QObject* list_slider = find_visual_item(visual_root, QStringLiteral("timelineMediaThumbnailSizeSlider"));
  if (panel->property("mediaViewMode").toString() != QStringLiteral("list") || list == nullptr ||
      list->property("count").toInt() != 4 ||
      list_slider == nullptr || list_slider->property("visible").toBool())
    return fail("List mode should retain all media and hide the grid-only size control");
  QQuickItem* video_row = find_media_row(visual_root, QStringLiteral("asset-video"));
  QQuickItem* list_filename = find_visual_item(video_row, QStringLiteral("timelineMediaAssetTitle"));
  QQuickItem* list_secondary_label = find_visual_item(
      video_row, QStringLiteral("timelineMediaAssetSecondaryLabel"));
  auto* list_accessible = QAccessible::queryAccessibleInterface(video_row);
  if (list_filename == nullptr ||
      list_filename->property("text").toString() != QStringLiteral("interview-wide-camera.mov") ||
      list_secondary_label == nullptr || !list_secondary_label->property("visible").toBool() ||
      !list_secondary_label->property("text").toString().contains(QStringLiteral("Canon EOS 80D")) ||
      list_accessible == nullptr ||
      list_accessible->text(QAccessible::Name) != QStringLiteral("interview-wide-camera.mov"))
    return fail("List rows should lead with the source filename and keep the camera label secondary and accessible");
  if (!capture(window, evidence_directory, QStringLiteral("timeline-media-list.png")))
    return fail("Could not capture the list evidence image");

  QObject* grid_button = find_visual_item(visual_root, QStringLiteral("timelineMediaGridViewButton"));
  if (!invoke_no_arguments(grid_button, "clicked"))
    return fail("The grid view toggle should be clickable");
  settle(100);
  if (panel->property("mediaViewMode").toString() != QStringLiteral("grid") ||
      find_visual_item(visual_root, QStringLiteral("timelineVirtualizedMediaGrid")) == nullptr)
    return fail("Switching back should recreate the virtualized grid view");

  if (catalog->property("visibleRangeCallCount").toInt() < 2)
    return fail("The active media view should drive visible-range thumbnail prioritization");

  panel->setProperty("workspaceActive", false);
  settle(80);
  const auto inactive_range_calls = catalog->property("visibleRangeCallCount").toInt();
  const auto inactive_results = panel->property("mediaResultCountValue").toInt();
  panel->setProperty("mediaSearchText", QStringLiteral("room-tone"));
  settle(220);
  if (panel->property("mediaBrowserActive").toBool() ||
      panel->property("sourceViewerActive").toBool() ||
      panel->property("mediaResultCountValue").toInt() != inactive_results ||
      catalog->property("visibleRangeCallCount").toInt() != inactive_range_calls)
    return fail("Inactive Timeline filtering must pause and leave the foreground catalog range alone");
  auto* source_player = find_object(panel, QStringLiteral("timelineSourcePreviewPlayer"));
  if (!source_player || !source_player->property("source").toUrl().isEmpty())
    return fail("The media browser must not retain a Source decoder");
  panel->setProperty("workspaceActive", true);
  settle(220);
  if (!panel->property("mediaBrowserActive").toBool() ||
      panel->property("mediaResultCountValue").toInt() != 1 ||
      !panel->property("mediaSearchText").toString().contains(QStringLiteral("room-tone")))
    return fail("Returning to Timeline must apply the pending search and restore its media results");

  panel->setProperty("mediaSearchText", QString{});
  catalog->setProperty("assets", QVariantList{});
  const QVariantMap project_photo_record{
      {QStringLiteral("assetId"), QStringLiteral("project-photo")},
      {QStringLiteral("mediaId"), QStringLiteral("project-photo")},
      {QStringLiteral("mediaType"), QStringLiteral("photo")},
      {QStringLiteral("filePath"), project_photo_path},
      {QStringLiteral("displayLabel"), QStringLiteral("Project photo")},
      {QStringLiteral("cameraLabel"), QStringLiteral("Project photo")},
      {QStringLiteral("thumbnailSource"), photo_thumbnail},
      {QStringLiteral("posterSource"), photo_thumbnail},
      {QStringLiteral("online"), true},
      {QStringLiteral("timelineReferenced"), true},
      {QStringLiteral("catalogAssetIndex"), -1},
      {QStringLiteral("trackIds"), QStringList{QStringLiteral("project-v1")}},
      {QStringLiteral("projectNames"), QStringList{}}};
  const QVariantMap project_video{
      {QStringLiteral("assetId"), QStringLiteral("project-video")},
      {QStringLiteral("mediaId"), QStringLiteral("project-video")},
      {QStringLiteral("mediaType"), QStringLiteral("video")},
      {QStringLiteral("filePath"), project_video_path},
      {QStringLiteral("displayLabel"), QStringLiteral("Project video")},
      {QStringLiteral("cameraLabel"), QStringLiteral("Project video")},
      {QStringLiteral("thumbnailSource"), video_thumbnail},
      {QStringLiteral("posterSource"), video_thumbnail},
      {QStringLiteral("durationSeconds"), 2.0},
      {QStringLiteral("durationText"), QStringLiteral("00:02")},
      {QStringLiteral("online"), true},
      {QStringLiteral("timelineReferenced"), true},
      {QStringLiteral("catalogAssetIndex"), -1},
      {QStringLiteral("trackIds"), QStringList{QStringLiteral("project-v1")}},
      {QStringLiteral("projectNames"), QStringList{}}};
  const QVariantList project_rows{project_photo_record, project_video, project_video};
  document->setProperty("usedMediaRows", project_rows);
  if (!QMetaObject::invokeMethod(document, "clipsChanged", Qt::DirectConnection))
    return fail("The project media snapshot should refresh from native clip-change notification");
  settle(240);
  if (catalog->property("assetCount").toInt() != 0 ||
      catalog->property("totalAssetCount").toInt() != 0 ||
      document->property("usedMediaSnapshotCallCount").toInt() < 2 ||
      panel->property("mediaResultCountValue").toInt() != 2 ||
      panel->property("projectReferencedMedia").toList().size() != 2)
    return fail("An empty global catalog should still show two unique project-referenced source files");
  grid = find_visual_item(visual_root, QStringLiteral("timelineVirtualizedMediaGrid"));
  grid_item = qobject_cast<QQuickItem*>(grid);
  QQuickItem* project_video_card = find_media_card(visual_root, QStringLiteral("project-video"));
  QQuickItem* project_photo_card = find_media_card(visual_root, QStringLiteral("project-photo"));
  if (grid_item == nullptr || grid->property("count").toInt() != 2 ||
      project_video_card == nullptr || project_photo_card == nullptr ||
      project_video_card->property("assetIndex").toInt() != -1 ||
      project_photo_card->property("assetIndex").toInt() != -1 ||
      catalog->property("lastVisibleFirst").toInt() != -1 ||
      catalog->property("lastVisibleLast").toInt() != -1)
    return fail("Project-only sources should appear once each in the media grid when Library is empty");

  if (!scroll_item_into_view(grid_item, project_video_card))
    return fail("The project-only video should be scrollable to its Source preview action");
  const int source_loads_before_project_preview = document->property("sourceLoadCount").toInt();
  const int source_tabs_before_project_preview = fixture->property("sourceTabRequests").toInt();
  QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier,
                     pointer_point(project_video_card,
                         QPointF(project_video_card->width() / 2,
                                 project_video_card->height() / 2)));
  settle(100);
  if (document->property("sourceLoadCount").toInt() != source_loads_before_project_preview + 1 ||
      document->property("lastLoadedAssetId").toString() != QStringLiteral("project-video") ||
      document->property("lastLoadedFilePath").toString() != project_video_path ||
      catalog->property("assetCount").toInt() != 0 ||
      catalog->property("totalAssetCount").toInt() != 0 ||
      fixture->property("sourceTabRequests").toInt() != source_tabs_before_project_preview + 1)
    return fail("Previewing a project-only source should pass its exact native file path into Source");

  if (!scroll_item_into_view(grid_item, project_photo_card))
    return fail("The project-only photo should remain available for timeline insertion");
  click_item(window, project_photo_card,
             QPointF(project_photo_card->width() / 2, project_photo_card->height() / 2));
  settle(50);
  insert_button = find_visual_item(visual_root, QStringLiteral("timelineMediaSelectedAddToTimelineButton"));
  insert_button_item = qobject_cast<QQuickItem*>(insert_button);
  if (insert_button_item == nullptr || !insert_button->property("visible").toBool())
    return fail("A project-only source should expose the native Add to Timeline action");
  click_item(window, insert_button_item,
             QPointF(insert_button_item->width() / 2, insert_button_item->height() / 2));
  settle(50);
  if (document->property("lastInsertedAssetId").toString() != QStringLiteral("project-photo") ||
      document->property("lastInsertedFilePath").toString() != project_photo_path ||
      catalog->property("assetCount").toInt() != 0 ||
      catalog->property("totalAssetCount").toInt() != 0)
    return fail("Adding a project-only source should preserve its exact file path and stable asset ID");

  // Add the same stable ID from the global catalog at a different path. The
  // project DTO owns the source location; catalog descriptors remain available
  // without granting edits against the unrelated Library file.
  QVariantMap catalog_duplicate{
      {QStringLiteral("assetId"), QStringLiteral("project-photo")},
      {QStringLiteral("mediaType"), QStringLiteral("photo")},
      {QStringLiteral("filePath"), catalog_photo_path},
      {QStringLiteral("cameraLabel"), QStringLiteral("Catalog descriptive metadata")},
      {QStringLiteral("thumbnailSource"), photo_thumbnail},
      {QStringLiteral("durationText"), QString{}},
      {QStringLiteral("durationSeconds"), 0.0},
      {QStringLiteral("online"), true},
      {QStringLiteral("captureDate"), QString{}},
      {QStringLiteral("lensModel"), QString{}},
      {QStringLiteral("projectNames"), QStringList{}}};
  catalog->setProperty("assets", QVariantList{catalog_duplicate});
  settle(260);
  grid = find_visual_item(visual_root, QStringLiteral("timelineVirtualizedMediaGrid"));
  grid_item = qobject_cast<QQuickItem*>(grid);
  const auto catalog_assets_after_union = catalog->property("assets").toList();
  const auto retained_catalog_asset = catalog_assets_after_union.size() == 1
      ? catalog_assets_after_union.first().toMap() : QVariantMap{};
  if (catalog->property("assetCount").toInt() != 1 ||
      catalog->property("totalAssetCount").toInt() != 1 ||
      retained_catalog_asset.value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("project-photo") ||
      retained_catalog_asset.value(QStringLiteral("filePath")).toString() != catalog_photo_path ||
      panel->property("mediaResultCountValue").toInt() != 2 ||
      panel->property("projectOnlyMediaCount").toInt() != 1 ||
      (grid != nullptr && grid->property("count").toInt() != 2) ||
      find_media_card(visual_root, QStringLiteral("project-photo")) == nullptr ||
      find_media_card(visual_root, QStringLiteral("project-video")) == nullptr ||
      document->property("lastInsertedFilePath").toString() != project_photo_path)
    return fail("Catalog/project union should keep one stable-ID row without changing the catalog file");

  project_photo_card = find_media_card(visual_root, QStringLiteral("project-photo"));
  if (project_photo_card == nullptr ||
      project_photo_card->property("filePath").toString() != project_photo_path ||
      project_photo_card->property("cameraLabel").toString() !=
          QStringLiteral("Catalog descriptive metadata") ||
      project_photo_card->property("assetIndex").toInt() != 0 ||
      project_photo_card->property("catalogBacked").toBool())
    return fail("A source-ID collision should retain descriptive catalog fields, use the project filepath, and disable catalog metadata edits");

  auto metadata_mutations_before_source_collision =
      document->property("metadataMutationCount").toInt();
  panel->setProperty("mediaTagsDraft", QStringLiteral("keep-project-tags-local"));
  project_photo_card->forceActiveFocus(Qt::OtherFocusReason);
  if (!project_photo_card->property("activeFocus").toBool())
    return fail("The project source card should receive focus for catalog-edit shortcut checks");
  QTest::keyClick(window, Qt::Key_3);
  QTest::keyClick(window, Qt::Key_T);
  settle(40);
  if (document->property("metadataMutationCount").toInt() !=
          metadata_mutations_before_source_collision ||
      panel->property("mediaTagsDraft").toString() != QStringLiteral("keep-project-tags-local"))
    return fail("Rating and tag shortcuts should not mutate global catalog metadata for a different project filepath");

  if (!scroll_item_into_view(grid_item, project_photo_card))
    return fail("The collided project source should remain scrollable for Source preview");
  const int source_loads_before_collision_preview = document->property("sourceLoadCount").toInt();
  const int source_tabs_before_collision_preview = fixture->property("sourceTabRequests").toInt();
  QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier,
                     pointer_point(project_photo_card,
                         QPointF(project_photo_card->width() / 2,
                                 project_photo_card->height() / 2)));
  settle(80);
  if (document->property("sourceLoadCount").toInt() != source_loads_before_collision_preview + 1 ||
      document->property("lastLoadedAssetId").toString() != QStringLiteral("project-photo") ||
      document->property("lastLoadedFilePath").toString() != project_photo_path ||
      fixture->property("sourceTabRequests").toInt() != source_tabs_before_collision_preview + 1)
    return fail("Previewing a catalog-collided ID should still open the exact project source file");

  project_photo_card = find_media_card(visual_root, QStringLiteral("project-photo"));
  grid_item = qobject_cast<QQuickItem*>(grid);
  if (!scroll_item_into_view(grid_item, project_photo_card))
    return fail("The collided project source should remain available for drag routing");
  QQuickItem* project_photo_preview = find_visual_item(
      project_photo_card, QStringLiteral("timelineMediaCardPreview"));
  if (project_photo_preview == nullptr)
    return fail("The collided project source should expose its draggable preview area");
  const QPoint project_drag_start = pointer_point(
      project_photo_preview,
      QPointF(project_photo_preview->width() / 2, project_photo_preview->height() / 2));
  const QPoint project_drag_end = project_drag_start + QPoint(30, 3);
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, project_drag_start);
  QTest::mouseMove(window, project_drag_end, 50);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, project_drag_end);
  settle(60);
  if (fixture->property("lastDroppedAssetId").toString() != QStringLiteral("project-photo") ||
      fixture->property("lastDroppedFilePath").toString() != project_photo_path)
    return fail("Dragging a catalog-collided source should carry its exact project filepath");

  project_photo_card = find_media_card(visual_root, QStringLiteral("project-photo"));
  click_item(window, project_photo_card,
             QPointF(project_photo_card->width() / 2, project_photo_card->height() / 2));
  settle(40);
  insert_button = find_visual_item(visual_root, QStringLiteral("timelineMediaSelectedAddToTimelineButton"));
  insert_button_item = qobject_cast<QQuickItem*>(insert_button);
  if (insert_button_item == nullptr || !insert_button->property("visible").toBool())
    return fail("The collided project source should keep its Add to Timeline action");
  click_item(window, insert_button_item,
             QPointF(insert_button_item->width() / 2, insert_button_item->height() / 2));
  settle(50);
  if (document->property("lastInsertedAssetId").toString() != QStringLiteral("project-photo") ||
      document->property("lastInsertedFilePath").toString() != project_photo_path ||
      document->property("metadataMutationCount").toInt() != metadata_mutations_before_source_collision ||
      catalog->property("assetCount").toInt() != 1 ||
      catalog->property("assets").toList().first().toMap()
          .value(QStringLiteral("filePath")).toString() != catalog_photo_path)
    return fail("Insert should preserve the project path while global catalog metadata and membership remain untouched");

  panel->setProperty("mediaSearchText", QStringLiteral("project-only-video.mp4"));
  settle(240);
  grid = find_visual_item(visual_root, QStringLiteral("timelineVirtualizedMediaGrid"));
  if (panel->property("mediaResultCountValue").toInt() != 1 || grid == nullptr ||
      grid->property("count").toInt() != 1 ||
      find_media_card(visual_root, QStringLiteral("project-video")) == nullptr ||
      find_media_card(visual_root, QStringLiteral("project-photo")) != nullptr)
    return fail("Filename search should include project-only media and return its matching source file");
  panel->setProperty("mediaSearchText", QString{});
  settle(240);

  const int snapshots_before_coalesced_notifications =
      document->property("usedMediaSnapshotCallCount").toInt();
  QSignalSpy project_snapshot_spy(panel, SIGNAL(projectReferencedMediaChanged()));
  QPointer<QQuickItem> photo_delegate_before_notifications =
      find_media_card(visual_root, QStringLiteral("project-photo"));
  QPointer<QQuickItem> video_delegate_before_notifications =
      find_media_card(visual_root, QStringLiteral("project-video"));
  if (photo_delegate_before_notifications.isNull() || video_delegate_before_notifications.isNull())
    return fail("Both project media delegates should be present before the no-churn notification burst");
  auto* media_model = find_media_model_with_asset_ids(
      panel, QStringList{QStringLiteral("project-photo"), QStringLiteral("project-video")});
  if (!project_snapshot_spy.isValid() || media_model == nullptr)
    return fail("The snapshot and virtualized-grid model should expose native change notifications for the no-churn proof");
  QSignalSpy model_rows_inserted_spy(media_model, &QAbstractItemModel::rowsInserted);
  QSignalSpy model_rows_removed_spy(media_model, &QAbstractItemModel::rowsRemoved);
  QSignalSpy model_reset_spy(media_model, &QAbstractItemModel::modelReset);
  if (!model_rows_inserted_spy.isValid() || !model_rows_removed_spy.isValid() ||
      !model_reset_spy.isValid())
    return fail("The active media model should expose row and reset notifications for the no-churn proof");
  if (!QMetaObject::invokeMethod(document, "clipsChanged", Qt::DirectConnection) ||
      !QMetaObject::invokeMethod(document, "sequenceChanged", Qt::DirectConnection) ||
      !QMetaObject::invokeMethod(document, "timelineStateChanged", Qt::DirectConnection))
    return fail("The fixture should dispatch all three native timeline notifications");
  settle(240);
  if (document->property("usedMediaSnapshotCallCount").toInt() !=
          snapshots_before_coalesced_notifications + 1 ||
      project_snapshot_spy.count() != 0 || model_rows_inserted_spy.count() != 0 ||
      model_rows_removed_spy.count() != 0 || model_reset_spy.count() != 0 ||
      find_media_card(visual_root, QStringLiteral("project-photo")) !=
          photo_delegate_before_notifications.data() ||
      find_media_card(visual_root, QStringLiteral("project-video")) !=
          video_delegate_before_notifications.data() ||
      panel->property("mediaResultCountValue").toInt() != 2)
    return fail("Equivalent clip/sequence/timeline notifications should refresh one snapshot without replacing project rows or churning the media model");

  const int snapshots_before_hidden_notifications =
      document->property("usedMediaSnapshotCallCount").toInt();
  panel->setProperty("workspaceActive", false);
  QVariantMap changed_hidden_video = project_video;
  changed_hidden_video.insert(QStringLiteral("displayLabel"), QStringLiteral("Updated while hidden"));
  document->setProperty("usedMediaRows",
                        QVariantList{project_photo_record, changed_hidden_video, changed_hidden_video});
  if (!QMetaObject::invokeMethod(document, "clipsChanged", Qt::DirectConnection) ||
      !QMetaObject::invokeMethod(document, "sequenceChanged", Qt::DirectConnection) ||
      !QMetaObject::invokeMethod(document, "timelineStateChanged", Qt::DirectConnection))
    return fail("The hidden-workspace fixture should dispatch all timeline notifications");
  settle(180);
  if (document->property("usedMediaSnapshotCallCount").toInt() !=
          snapshots_before_hidden_notifications ||
      panel->property("projectReferencedMedia").toList().at(1).toMap()
          .value(QStringLiteral("displayLabel")).toString() != QStringLiteral("Project video"))
    return fail("Hidden Timeline notifications should defer the used-media snapshot refresh");
  panel->setProperty("workspaceActive", true);
  settle(240);
  grid = find_visual_item(visual_root, QStringLiteral("timelineVirtualizedMediaGrid"));
  project_video_card = find_media_card(visual_root, QStringLiteral("project-video"));
  if (document->property("usedMediaSnapshotCallCount").toInt() !=
          snapshots_before_hidden_notifications + 1 ||
      panel->property("projectReferencedMedia").toList().at(1).toMap()
          .value(QStringLiteral("displayLabel")).toString() != QStringLiteral("Updated while hidden") ||
      panel->property("mediaResultCountValue").toInt() != 2 || grid == nullptr ||
      grid->property("count").toInt() != 2 || project_video_card == nullptr)
    return fail("Reactivating the media workspace should apply one deferred DTO refresh while preserving the two unique sources");
  return EXIT_SUCCESS;
}
