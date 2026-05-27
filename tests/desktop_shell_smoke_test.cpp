#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QSet>
#include <QStringList>
#include <QTime>
#include <QVariant>
#include <qqml.h>
#include <QQuickStyle>
#include <QTemporaryDir>

#include "app_shell_controller.h"
#include "brush_live_preview_item.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "editor_preview_texture_item.h"
#include "export_job_controller.h"
#include "job_queue_model.h"
#include "job_runner.h"
#include "macos_updater.h"
#include "preview_backend_controller.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::CatalogAssetItem make_asset(const QString& asset_id, const QString& file_name) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Test Camera");
  item.file_path = QStringLiteral("/tmp/%1").arg(file_name);
  item.capture_date = QStringLiteral("Apr 8, 2026");
  item.capture_date_value = QDate(2026, 4, 8);
  item.imported_at = QStringLiteral("Apr 8, 2026  9:00 AM");
  item.imported_at_value = QDateTime(QDate(2026, 4, 8), QTime(9, 0));
  item.media_type = QStringLiteral("photo");
  item.online = true;
  return item;
}

QObject* find_named_object_recursive(QObject* root, const QString& object_name, QSet<const QObject*>& visited) {
  if (root == nullptr || visited.contains(root)) {
    return nullptr;
  }
  visited.insert(root);
  if (root->objectName() == object_name) {
    return root;
  }
  if (auto* item = qobject_cast<QQuickItem*>(root); item != nullptr) {
    for (auto* child_item : item->childItems()) {
      if (auto* found = find_named_object_recursive(child_item, object_name, visited); found != nullptr) {
        return found;
      }
    }
  }
  for (auto* child : root->children()) {
    if (auto* found = find_named_object_recursive(child, object_name, visited); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

QObject* find_named_object(QObject* root, const QString& object_name) {
  QSet<const QObject*> visited;
  return find_named_object_recursive(root, object_name, visited);
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));

  QApplication app(argc, argv);
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("DesktopShellSmokeTest"));

  QTemporaryDir runtime_root;
  QTemporaryDir cache_root;
  if (!runtime_root.isValid() || !cache_root.isValid()) {
    return fail("temporary runtime directories should be created");
  }

  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArray("postgres://example/test"));

  photo_editor::desktop::AppShellController app_shell;
  photo_editor::desktop::CatalogAssetModel catalog_assets;
  photo_editor::desktop::CarouselDocumentController carousel_document;
  photo_editor::desktop::EditorPreviewController editor_preview;
  photo_editor::desktop::JobRunner job_runner;
  photo_editor::desktop::JobQueueModel job_queue;
  photo_editor::desktop::ExportJobController export_jobs;
  photo_editor::desktop::MacosUpdater macos_updater;
  photo_editor::desktop::PreviewBackendController preview_backend;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);
  qmlRegisterType<photo_editor::desktop::BrushLivePreviewItem>(
      "PhotoEditor",
      1,
      0,
      "BrushLivePreviewItem");
  qmlRegisterType<photo_editor::desktop::EditorPreviewTextureItem>(
      "PhotoEditor",
      1,
      0,
      "EditorPreviewTextureItem");

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("appShell", &app_shell);
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
  engine.rootContext()->setContextProperty("exportJobs", &export_jobs);
  engine.rootContext()->setContextProperty("jobQueue", &job_queue);
  engine.rootContext()->setContextProperty("macosUpdater", &macos_updater);
  engine.rootContext()->setContextProperty("previewBackend", &preview_backend);
  engine.rootContext()->setContextProperty("timelineDocument", &timeline_document);
  engine.rootContext()->setContextProperty("timelinePlayback", &timeline_playback);

  const auto main_qml_path =
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(QStringLiteral("apps/desktop/qml/Main.qml"));
  engine.load(QUrl::fromLocalFile(main_qml_path));
  QCoreApplication::processEvents();

  if (engine.rootObjects().isEmpty()) {
    return fail("desktop shell should load Main.qml");
  }

  QObject* root = engine.rootObjects().constFirst();
  if (root == nullptr) {
    return fail("desktop shell should expose a root object");
  }

  root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
  QCoreApplication::processEvents();

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-1"), QStringLiteral("IMG_0001.jpg"))});
  catalog_assets.selectIndex(0);
  QCoreApplication::processEvents();
  if (editor_preview.assetId() != QStringLiteral("asset-1") || !editor_preview.hasAsset()) {
    return fail("edit workspace should load the selected catalog asset");
  }

  catalog_assets.replaceAssetsForTesting({});
  QCoreApplication::processEvents();
  if (!editor_preview.assetId().isEmpty() || editor_preview.hasAsset()) {
    return fail("edit workspace should clear the editor when the catalog selection disappears");
  }

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg"))});
  QCoreApplication::processEvents();
  if (catalog_assets.selectedAssetId() != QStringLiteral("asset-2") ||
      editor_preview.assetId() != QStringLiteral("asset-2")) {
    return fail("edit workspace should follow the next visible asset after the gallery repopulates");
  }

  auto* edit_workspace = root->findChild<QObject*>(QStringLiteral("editStudioWorkspace"));
  if (edit_workspace == nullptr) {
    return fail("edit workspace should expose a testable StudioWorkspace object");
  }
  auto* workspace_tab_edit = find_named_object(root, QStringLiteral("workspaceTabEdit"));
  auto* workspace_tab_edit_label = find_named_object(root, QStringLiteral("workspaceTabLabelEdit"));
  auto* top_workspace_status_label = find_named_object(root, QStringLiteral("topWorkspaceStatusLabel"));
  auto* top_selection_status_label = find_named_object(root, QStringLiteral("topSelectionStatusLabel"));
  if (workspace_tab_edit == nullptr || workspace_tab_edit_label == nullptr ||
      top_workspace_status_label == nullptr || top_selection_status_label == nullptr ||
      workspace_tab_edit->property("text").toString() != QStringLiteral("Edit") ||
      workspace_tab_edit_label->property("text").toString() != QStringLiteral("Edit") ||
      top_workspace_status_label->property("text").toString().startsWith(QStringLiteral("EDIT")) ||
      top_selection_status_label->property("text").toString() == QStringLiteral("NO ACTIVE ASSET")) {
    return fail("global workspace chrome should use readable title-case navigation and status labels");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  QVariant cool_kelvin;
  QVariant neutral_kelvin;
  QVariant warm_kelvin;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "whiteBalanceKelvin",
          Q_RETURN_ARG(QVariant, cool_kelvin),
          Q_ARG(QVariant, QVariant(-100))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "whiteBalanceKelvin",
          Q_RETURN_ARG(QVariant, neutral_kelvin),
          Q_ARG(QVariant, QVariant(0))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "whiteBalanceKelvin",
          Q_RETURN_ARG(QVariant, warm_kelvin),
          Q_ARG(QVariant, QVariant(100))) ||
      !(cool_kelvin.toInt() < neutral_kelvin.toInt() &&
        neutral_kelvin.toInt() < warm_kelvin.toInt())) {
    return fail("temperature slider Kelvin labels should increase in the warm direction");
  }

  const auto expect_slider = [&](const char* object_name,
                                 double expected_from,
                                 double expected_to,
                                 double expected_default,
                                 const char* failure_message) -> int {
    auto* slider = edit_workspace->findChild<QObject*>(QString::fromLatin1(object_name));
    if (slider == nullptr) {
      std::cerr << failure_message << ": slider should exist\n";
      return EXIT_FAILURE;
    }
    const auto actual_from = slider->property("from").toDouble();
    const auto actual_to = slider->property("to").toDouble();
    const auto actual_default = slider->property("defaultValue").toDouble();
    const auto modified = slider->property("modified").toBool();
    if (std::abs(actual_from - expected_from) > 1e-6 || std::abs(actual_to - expected_to) > 1e-6 ||
        std::abs(actual_default - expected_default) > 1e-6 || modified) {
      std::cerr << failure_message << ": expected range/default "
                << expected_from << ".." << expected_to << " default " << expected_default
                << ", got " << actual_from << ".." << actual_to << " default " << actual_default
                << ", modified=" << modified << "\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  };
  if (expect_slider("selectedToneWheelHueSlider", 0.0, 1.0, 0.0, "color grading hue slider") !=
          EXIT_SUCCESS ||
      expect_slider("channelMixerRedFromRedSlider", -200.0, 200.0, 100.0, "channel mixer identity slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplacementTargetHueSlider", 0.0, 1.0, 0.33, "color replacement target hue slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplacementRangeSlider", 1.0, 100.0, 35.0, "color replacement range slider") !=
          EXIT_SUCCESS ||
      expect_slider("skinToneLineHueSlider", 0.0, 1.0, 0.08, "skin tone line hue slider") != EXIT_SUCCESS ||
      expect_slider("cropZoomSlider", 0.5, 4.0, 1.0, "crop zoom slider") != EXIT_SUCCESS ||
      expect_slider("cropRectXSlider", 0.0, 100.0, 0.0, "crop rectangle x slider") !=
          EXIT_SUCCESS ||
      expect_slider("cropRectYSlider", 0.0, 100.0, 0.0, "crop rectangle y slider") !=
          EXIT_SUCCESS ||
      expect_slider("cropRectWidthSlider", 5.0, 100.0, 100.0, "crop rectangle width slider") !=
          EXIT_SUCCESS ||
      expect_slider("cropRectHeightSlider", 5.0, 100.0, 100.0, "crop rectangle height slider") !=
          EXIT_SUCCESS ||
      expect_slider("transformScaleSlider", 0.25, 4.0, 1.0, "transform scale slider") != EXIT_SUCCESS ||
      expect_slider("liquifyRadiusSlider", 0.02, 1.0, 0.25, "liquify radius slider") != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }
  auto* white_balance_group = edit_workspace->findChild<QObject*>(QStringLiteral("whiteBalanceInspectorGroup"));
  auto* light_group = edit_workspace->findChild<QObject*>(QStringLiteral("lightInspectorGroup"));
  auto* tone_curve_group = edit_workspace->findChild<QObject*>(QStringLiteral("toneCurveInspectorGroup"));
  auto* color_group = edit_workspace->findChild<QObject*>(QStringLiteral("colorInspectorGroup"));
  auto* presence_group = edit_workspace->findChild<QObject*>(QStringLiteral("presenceInspectorGroup"));
  auto* edit_inspector_panel = edit_workspace->findChild<QObject*>(QStringLiteral("editInspectorPanel"));
  auto* adjust_inspector_tab = find_named_object(edit_workspace, QStringLiteral("adjustInspectorTabButton"));
  auto* scopes_inspector_tab = find_named_object(edit_workspace, QStringLiteral("scopesInspectorTabButton"));
  auto* detail_inspector_tab = find_named_object(edit_workspace, QStringLiteral("detailInspectorTabButton"));
  auto* info_inspector_tab = find_named_object(edit_workspace, QStringLiteral("infoInspectorTabButton"));
  auto* light_basic_tone_group = find_named_object(edit_workspace, QStringLiteral("lightBasicToneGroup"));
  auto* light_advanced_tone_group = find_named_object(edit_workspace, QStringLiteral("lightAdvancedToneGroup"));
  auto* light_levels_group = find_named_object(edit_workspace, QStringLiteral("lightLevelsGroup"));
  auto* light_zone_mapping_group = find_named_object(edit_workspace, QStringLiteral("lightZoneMappingGroup"));
  auto* advanced_color_group = find_named_object(edit_workspace, QStringLiteral("advancedColorInspectorGroup"));
  auto* color_mix_group = find_named_object(edit_workspace, QStringLiteral("colorMixGroup"));
  auto* channel_mixer_group = find_named_object(edit_workspace, QStringLiteral("channelMixerGroup"));
  auto* black_white_group = find_named_object(edit_workspace, QStringLiteral("blackWhiteGroup"));
  auto* color_replace_group = find_named_object(edit_workspace, QStringLiteral("colorReplaceGroup"));
  auto* skin_tone_group = find_named_object(edit_workspace, QStringLiteral("skinToneGroup"));
  auto* harmony_group = find_named_object(edit_workspace, QStringLiteral("harmonyGroup"));
  auto* split_toning_group = find_named_object(edit_workspace, QStringLiteral("splitToningGroup"));
  if (white_balance_group == nullptr || light_group == nullptr || tone_curve_group == nullptr ||
      color_group == nullptr || presence_group == nullptr ||
      !white_balance_group->property("visible").toBool() || !light_group->property("visible").toBool() ||
      !tone_curve_group->property("visible").toBool() || !color_group->property("visible").toBool() ||
      !presence_group->property("visible").toBool() ||
      !(white_balance_group->property("y").toDouble() < light_group->property("y").toDouble() &&
        light_group->property("y").toDouble() < tone_curve_group->property("y").toDouble() &&
        tone_curve_group->property("y").toDouble() < color_group->property("y").toDouble() &&
        color_group->property("y").toDouble() < presence_group->property("y").toDouble())) {
    return fail("adjust inspector should order primary controls as white balance, light, tone curve, color, then presence");
  }
  if (edit_inspector_panel == nullptr || adjust_inspector_tab == nullptr ||
      scopes_inspector_tab == nullptr || detail_inspector_tab == nullptr || info_inspector_tab == nullptr ||
      edit_inspector_panel->property("title").toString() != QStringLiteral("Adjust") ||
      adjust_inspector_tab->property("text").toString() != QStringLiteral("Adjust") ||
      scopes_inspector_tab->property("text").toString() != QStringLiteral("Scopes") ||
      detail_inspector_tab->property("text").toString() != QStringLiteral("Detail") ||
      info_inspector_tab->property("text").toString() != QStringLiteral("Info") ||
      white_balance_group->property("title").toString() != QStringLiteral("White Balance") ||
      tone_curve_group->property("title").toString() != QStringLiteral("Tone Curve")) {
    return fail("adjust inspector should use readable title-case panel, tab, and group labels");
  }
  if (light_basic_tone_group == nullptr || light_advanced_tone_group == nullptr ||
      light_levels_group == nullptr || light_zone_mapping_group == nullptr ||
      light_basic_tone_group->property("title").toString() != QStringLiteral("Basic Tone") ||
      light_advanced_tone_group->property("title").toString() != QStringLiteral("Advanced Tone") ||
      light_levels_group->property("title").toString() != QStringLiteral("Levels") ||
      light_zone_mapping_group->property("title").toString() != QStringLiteral("Zone Mapping") ||
      !light_basic_tone_group->property("expanded").toBool() ||
      light_advanced_tone_group->property("expanded").toBool() ||
      light_levels_group->property("expanded").toBool() ||
      light_zone_mapping_group->property("expanded").toBool() ||
      !(light_basic_tone_group->property("y").toDouble() <
        light_advanced_tone_group->property("y").toDouble() &&
        light_advanced_tone_group->property("y").toDouble() <
        light_levels_group->property("y").toDouble() &&
        light_levels_group->property("y").toDouble() <
        light_zone_mapping_group->property("y").toDouble())) {
    return fail("light inspector should keep basic tone visible and tuck advanced tone, levels, and zones into ordered collapsible groups");
  }
  edit_workspace->setProperty("lightAdvancedToneExpanded", true);
  edit_workspace->setProperty("lightLevelsExpanded", true);
  edit_workspace->setProperty("lightZonesExpanded", true);
  QCoreApplication::processEvents();
  if (!light_advanced_tone_group->property("expanded").toBool() ||
      !light_levels_group->property("expanded").toBool() ||
      !light_zone_mapping_group->property("expanded").toBool()) {
    return fail("light inspector advanced groups should expand through explicit workspace state");
  }
  edit_workspace->setProperty("lightAdvancedToneExpanded", false);
  edit_workspace->setProperty("lightLevelsExpanded", false);
  edit_workspace->setProperty("lightZonesExpanded", false);
  QCoreApplication::processEvents();
  if (advanced_color_group == nullptr || color_mix_group == nullptr || channel_mixer_group == nullptr ||
      black_white_group == nullptr || color_replace_group == nullptr || skin_tone_group == nullptr ||
      harmony_group == nullptr || split_toning_group == nullptr ||
      advanced_color_group->property("title").toString() != QStringLiteral("Advanced Color") ||
      color_mix_group->property("title").toString() != QStringLiteral("Color Mix") ||
      channel_mixer_group->property("title").toString() != QStringLiteral("Channel Mixer") ||
      black_white_group->property("title").toString() != QStringLiteral("Black & White") ||
      color_replace_group->property("title").toString() != QStringLiteral("Color Replace") ||
      skin_tone_group->property("title").toString() != QStringLiteral("Skin Tone") ||
      harmony_group->property("title").toString() != QStringLiteral("Harmony") ||
      split_toning_group->property("title").toString() != QStringLiteral("Split Toning") ||
      !color_mix_group->property("expanded").toBool() ||
      channel_mixer_group->property("expanded").toBool() ||
      black_white_group->property("expanded").toBool() ||
      color_replace_group->property("expanded").toBool() ||
      skin_tone_group->property("expanded").toBool() ||
      harmony_group->property("expanded").toBool() ||
      split_toning_group->property("expanded").toBool()) {
    return fail("advanced color inspector should keep color mix available while collapsing specialist color tools by default");
  }
  edit_workspace->setProperty("channelMixerExpanded", true);
  edit_workspace->setProperty("blackWhiteExpanded", true);
  edit_workspace->setProperty("colorReplaceExpanded", true);
  edit_workspace->setProperty("skinToneExpanded", true);
  edit_workspace->setProperty("harmonyExpanded", true);
  edit_workspace->setProperty("splitToningExpanded", true);
  QCoreApplication::processEvents();
  if (!channel_mixer_group->property("expanded").toBool() ||
      !black_white_group->property("expanded").toBool() ||
      !color_replace_group->property("expanded").toBool() ||
      !skin_tone_group->property("expanded").toBool() ||
      !harmony_group->property("expanded").toBool() ||
      !split_toning_group->property("expanded").toBool()) {
    return fail("specialist color tools should expand through explicit workspace state");
  }
  edit_workspace->setProperty("channelMixerExpanded", false);
  edit_workspace->setProperty("blackWhiteExpanded", false);
  edit_workspace->setProperty("colorReplaceExpanded", false);
  edit_workspace->setProperty("skinToneExpanded", false);
  edit_workspace->setProperty("harmonyExpanded", false);
  edit_workspace->setProperty("splitToningExpanded", false);
  QCoreApplication::processEvents();
  auto* tone_curve_editor = edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditor"));
  auto* tone_curve_delete_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditorDeletePointButton"));
  auto* tone_curve_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditorResetCurveButton"));
  auto* tone_curve_mode_badge =
      edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditorChannelModeBadge"));
  if (tone_curve_editor == nullptr || tone_curve_delete_button == nullptr ||
      tone_curve_reset_button == nullptr || tone_curve_mode_badge == nullptr) {
    return fail("tone curve editor should expose visible channel, delete, and reset controls");
  }
  editor_preview.setCurveChannel(QStringLiteral("RGB"));
  editor_preview.setCurveShadows(18.0);
  QCoreApplication::processEvents();
  if (editor_preview.activeCurveMode() != QStringLiteral("Parametric") ||
      tone_curve_editor->property("channelModeLabel").toString() != QStringLiteral("RGB Parametric")) {
    return fail("tone curve editor should identify RGB parametric mode before point editing takes over");
  }
  editor_preview.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.48}, {QStringLiteral("y"), 0.56}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  QCoreApplication::processEvents();
  if (editor_preview.activeCurveMode() != QStringLiteral("Point") ||
      tone_curve_editor->property("channelModeLabel").toString() != QStringLiteral("RGB Point") ||
      std::abs(editor_preview.curveShadows()) > 1e-6) {
    return fail("tone curve editor should identify point mode after drawing a curve and clear RGB parametric controls");
  }
  editor_preview.setCurveChannel(QStringLiteral("Red"));
  editor_preview.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.42}, {QStringLiteral("y"), 0.62}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  QCoreApplication::processEvents();
  QVariant selected_curve_index;
  if (tone_curve_editor->property("channelModeLabel").toString() != QStringLiteral("Red Point") ||
      tone_curve_editor->property("pointCount").toInt() != 3 ||
      tone_curve_editor->property("histogramPrimaryFillOpacity").toDouble() > 0.10 ||
      tone_curve_editor->property("histogramPrimaryStrokeOpacity").toDouble() > 0.28 ||
      tone_curve_editor->property("histogramRgbFillOpacity").toDouble() > 0.04 ||
      tone_curve_editor->property("histogramRgbStrokeOpacity").toDouble() > 0.12 ||
      !QMetaObject::invokeMethod(
          tone_curve_editor,
          "testSelectPoint",
          Q_RETURN_ARG(QVariant, selected_curve_index),
          Q_ARG(QVariant, QVariant(1))) ||
      selected_curve_index.toInt() != 1 ||
      !tone_curve_editor->property("selectedPointDeletable").toBool() ||
      !tone_curve_delete_button->property("enabled").toBool()) {
    return fail("tone curve editor should make channel mode, selected point deletion, and quiet histogram opacity testable");
  }
  QVariant deleted_curve_point;
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testDeleteSelectedPoint",
          Q_RETURN_ARG(QVariant, deleted_curve_point)) ||
      !deleted_curve_point.toBool()) {
    return fail("tone curve editor should delete the selected interior point through an explicit control path");
  }
  QCoreApplication::processEvents();
  if (editor_preview.activeCurvePoints().size() != 2) {
    return fail("tone curve selected-point delete should update the active channel curve");
  }
  QVariant reset_curve;
  editor_preview.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.16}},
      QVariantMap{{QStringLiteral("x"), 0.55}, {QStringLiteral("y"), 0.44}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.92}},
  });
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testResetActiveCurve",
          Q_RETURN_ARG(QVariant, reset_curve)) ||
      !reset_curve.toBool()) {
    return fail("tone curve editor should reset the active curve through an explicit control path");
  }
  QCoreApplication::processEvents();
  if (editor_preview.activeCurvePoints().size() != 2 ||
      !tone_curve_reset_button->property("enabled").toBool()) {
    return fail("tone curve reset should restore the active channel to the default curve");
  }
  QVariant previewed_curve_preset;
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testPreviewPreset",
          Q_RETURN_ARG(QVariant, previewed_curve_preset),
          Q_ARG(QVariant, QVariant(QStringLiteral("Filmic S")))) ||
      !previewed_curve_preset.toBool() ||
      tone_curve_editor->property("previewPointsModel").toList().size() < 5 ||
      editor_preview.activeCurvePoints().size() != 2) {
    return fail("tone curve preset preview should show preset points without mutating the active curve");
  }
  QVariant cleared_curve_preview;
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testClearPresetPreview",
          Q_RETURN_ARG(QVariant, cleared_curve_preview)) ||
      !cleared_curve_preview.toBool() ||
      !tone_curve_editor->property("previewPointsModel").toList().isEmpty() ||
      editor_preview.activeCurvePoints().size() != 2) {
    return fail("tone curve preset preview should be reversible without changing the recipe");
  }
  auto* brightness_slider = edit_workspace->findChild<QObject*>(QStringLiteral("brightnessSlider"));
  auto* tint_slider = edit_workspace->findChild<QObject*>(QStringLiteral("tintSlider"));
  if (brightness_slider == nullptr || tint_slider == nullptr ||
      std::abs(brightness_slider->property("defaultPosition").toDouble() - 0.5) > 1e-6 ||
      std::abs(tint_slider->property("defaultPosition").toDouble() - 0.5) > 1e-6) {
    return fail("signed adjustment sliders should expose a centered neutral/default position");
  }
  editor_preview.setBrightness(40.0);
  QCoreApplication::processEvents();
  if (brightness_slider->property("valueText").toString() != QStringLiteral("+40%") ||
      brightness_slider->property("normalizedValue").toDouble() <= 0.5 ||
      std::abs(brightness_slider->property("fillStartPosition").toDouble() - 0.5) > 1e-6 ||
      brightness_slider->property("fillEndPosition").toDouble() <= 0.5) {
    return fail("positive signed adjustments should show a plus percent label and fill right from neutral");
  }
  editor_preview.setBrightness(-40.0);
  QCoreApplication::processEvents();
  if (brightness_slider->property("valueText").toString() != QStringLiteral("-40%") ||
      brightness_slider->property("normalizedValue").toDouble() >= 0.5 ||
      brightness_slider->property("fillStartPosition").toDouble() >= 0.5 ||
      std::abs(brightness_slider->property("fillEndPosition").toDouble() - 0.5) > 1e-6) {
    return fail("negative signed adjustments should show a minus percent label and fill left from neutral");
  }
  editor_preview.setBrightness(0.0);
  editor_preview.setTint(25.0);
  QCoreApplication::processEvents();
  if (tint_slider->property("valueText").toString() != QStringLiteral("+25%") ||
      tint_slider->property("trackColorCount").toInt() < 2) {
    return fail("color signed adjustments should use signed percent text without losing directional track colors");
  }
  editor_preview.setTint(0.0);
  QCoreApplication::processEvents();
  auto* exposure_slider = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSlider"));
  auto* exposure_value_field = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSliderValueField"));
  auto* exposure_modified_marker = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSliderModifiedMarker"));
  auto* temperature_slider = edit_workspace->findChild<QObject*>(QStringLiteral("temperatureSlider"));
  auto* temperature_value_field = edit_workspace->findChild<QObject*>(QStringLiteral("temperatureSliderValueField"));
  auto* channel_mixer_red_slider = edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerRedFromRedSlider"));
  auto* color_replacement_hue_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplacementTargetHueSlider"));
  auto* selected_tone_hue_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("selectedToneWheelHueSlider"));
  auto* split_toning_shadows_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningShadowsWheel"));
  auto* split_toning_midtones_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningMidtonesWheel"));
  auto* split_toning_highlights_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningHighlightsWheel"));
  auto* liquify_radius_slider = edit_workspace->findChild<QObject*>(QStringLiteral("liquifyRadiusSlider"));
  QVariant committed_input_value;
  if (exposure_slider == nullptr || exposure_value_field == nullptr || exposure_modified_marker == nullptr ||
      temperature_slider == nullptr ||
      temperature_value_field == nullptr || !exposure_value_field->property("enabled").toBool() ||
      !temperature_value_field->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          exposure_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("+1.37 EV"))) ||
      std::abs(editor_preview.exposure() - 1.37) > 1e-6 ||
      exposure_slider->property("valueText").toString() != QStringLiteral("+1.37 EV") ||
      !exposure_slider->property("modified").toBool() ||
      !exposure_modified_marker->property("visible").toBool()) {
    return fail("precise exposure entry should accept typed EV values through the slider value field");
  }
  if (!QMetaObject::invokeMethod(
          temperature_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("6500K"))) ||
      std::abs(editor_preview.temperature() - ((6500.0 - 5500.0) / (12000.0 - 5500.0) * 100.0)) > 1e-6 ||
      temperature_slider->property("valueText").toString() != QStringLiteral("6500K")) {
    return fail("white balance temperature entry should accept typed Kelvin values through the slider value field");
  }
  if (color_replacement_hue_slider == nullptr || selected_tone_hue_slider == nullptr ||
      split_toning_shadows_wheel == nullptr || split_toning_midtones_wheel == nullptr ||
      split_toning_highlights_wheel == nullptr ||
      liquify_radius_slider == nullptr ||
      !QMetaObject::invokeMethod(
          color_replacement_hue_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("180°"))) ||
      std::abs(editor_preview.colorReplacementTargetHue() - 0.5) > 1e-6 ||
      color_replacement_hue_slider->property("valueText").toString() != QStringLiteral("180°") ||
      color_replacement_hue_slider->property("valueInputMode").toString() !=
          QStringLiteral("degreesNormalized")) {
    return fail("color hue sliders should accept typed degree values instead of raw normalized values");
  }
  if (split_toning_shadows_wheel->property("minimumUsefulWidth").toInt() > 96 ||
      split_toning_midtones_wheel->property("minimumUsefulWidth").toInt() > 96 ||
      split_toning_highlights_wheel->property("minimumUsefulWidth").toInt() > 96) {
    return fail("split-toning wheels should stay compact enough to fit three-up in the inspector");
  }
  if (!QMetaObject::invokeMethod(
          selected_tone_hue_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("90°"))) ||
      std::abs(editor_preview.gradingMidtonesHue() - 0.25) > 1e-6 ||
      selected_tone_hue_slider->property("valueText").toString() != QStringLiteral("90°")) {
    return fail("color grading hue slider typed values should match the displayed degree unit");
  }
  if (!QMetaObject::invokeMethod(
          liquify_radius_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("33%"))) ||
      std::abs(editor_preview.liquifyRadius() - 0.33) > 1e-6 ||
      liquify_radius_slider->property("valueText").toString() != QStringLiteral("33%") ||
      liquify_radius_slider->property("valueInputMode").toString() != QStringLiteral("percentNormalized")) {
    return fail("normalized percent sliders should accept typed percentages instead of raw 0..1 values");
  }
  editor_preview.setExposure(0.0);
  editor_preview.setTemperature(0.0);
  editor_preview.setColorReplacementTargetHue(0.33);
  editor_preview.setGradingMidtonesHue(0.0);
  editor_preview.setLiquifyRadius(0.25);
  QCoreApplication::processEvents();
  editor_preview.setExposure(0.85);
  editor_preview.setChannelMixerRedFromRed(42.0);
  QCoreApplication::processEvents();
  if (exposure_slider == nullptr || channel_mixer_red_slider == nullptr ||
      exposure_slider->property("valueText").toString() != QStringLiteral("+0.85 EV") ||
      !exposure_slider->property("modified").toBool() ||
      !channel_mixer_red_slider->property("modified").toBool() ||
      !QMetaObject::invokeMethod(exposure_slider, "resetRequested") ||
      !QMetaObject::invokeMethod(channel_mixer_red_slider, "resetRequested")) {
    return fail("adjustment sliders should expose a testable double-click reset signal");
  }
  QCoreApplication::processEvents();
  if (std::abs(editor_preview.exposure()) > 1e-6 ||
      std::abs(editor_preview.channelMixerRedFromRed() - 100.0) > 1e-6 ||
      exposure_slider->property("modified").toBool() ||
      exposure_modified_marker->property("visible").toBool() ||
      channel_mixer_red_slider->property("modified").toBool()) {
    return fail("double-click reset should restore each adjustment slider to its declared default value");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("detail"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* crop_box_group = find_named_object(edit_workspace, QStringLiteral("cropBoxGroup"));
  auto* crop_guide_aspect_group = find_named_object(edit_workspace, QStringLiteral("cropGuideAspectGroup"));
  auto* crop_transform_group = find_named_object(edit_workspace, QStringLiteral("cropTransformGroup"));
  auto* crop_liquify_group = find_named_object(edit_workspace, QStringLiteral("cropLiquifyGroup"));
  if (crop_box_group == nullptr || crop_guide_aspect_group == nullptr ||
      crop_transform_group == nullptr || crop_liquify_group == nullptr ||
      crop_box_group->property("title").toString() != QStringLiteral("Crop Box") ||
      crop_guide_aspect_group->property("title").toString() != QStringLiteral("Guides & Aspect") ||
      crop_transform_group->property("title").toString() != QStringLiteral("Transform & Upright") ||
      crop_liquify_group->property("title").toString() != QStringLiteral("Liquify") ||
      !crop_box_group->property("expanded").toBool() ||
      !crop_guide_aspect_group->property("expanded").toBool() ||
      crop_transform_group->property("expanded").toBool() ||
      crop_liquify_group->property("expanded").toBool()) {
    return fail("crop inspector should keep crop box and guide/aspect controls visible while collapsing advanced transform and liquify sections");
  }
  edit_workspace->setProperty("cropTransformExpanded", true);
  edit_workspace->setProperty("cropLiquifyExpanded", true);
  QCoreApplication::processEvents();
  if (!crop_transform_group->property("expanded").toBool() ||
      !crop_liquify_group->property("expanded").toBool()) {
    return fail("crop inspector advanced transform and liquify sections should expand through workspace state");
  }
  edit_workspace->setProperty("cropTransformExpanded", false);
  edit_workspace->setProperty("cropLiquifyExpanded", false);
  QCoreApplication::processEvents();
  const QStringList crop_guide_button_names{
      QStringLiteral("cropGuideOffButton"),
      QStringLiteral("cropGuideThirdsButton"),
      QStringLiteral("cropGuideGoldenButton"),
      QStringLiteral("cropGuideCenterButton"),
      QStringLiteral("cropGuideDiagonalButton"),
      QStringLiteral("cropGuideSpiralButton"),
  };
  for (const auto& button_name : crop_guide_button_names) {
    auto* button = edit_workspace->findChild<QObject*>(button_name);
    if (button == nullptr || !button->property("visible").toBool()) {
      return fail("crop inspector should expose all guide overlay choices as visible controls");
    }
  }

  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setCropToolMode",
          Q_ARG(QVariant, QVariant(QStringLiteral("transform"))))) {
    return fail("edit workspace should expose crop submode switching");
  }
  QCoreApplication::processEvents();
  auto* crop_tool_hud = edit_workspace->findChild<QObject*>(QStringLiteral("cropToolHud"));
  auto* crop_tool_header_label = edit_workspace->findChild<QObject*>(QStringLiteral("cropToolHeaderLabel"));
  auto* crop_active_canvas_border = edit_workspace->findChild<QObject*>(QStringLiteral("cropActiveCanvasBorder"));
  auto* crop_box_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("cropBoxOverlay"));
  auto* crop_horizon_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("cropHorizonOverlay"));
  auto* crop_transform_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("cropTransformOverlay"));
  if (crop_tool_hud == nullptr || crop_tool_header_label == nullptr || crop_active_canvas_border == nullptr ||
      crop_box_overlay == nullptr || crop_horizon_overlay == nullptr || crop_transform_overlay == nullptr ||
      !crop_tool_hud->property("visible").toBool() || !crop_active_canvas_border->property("visible").toBool() ||
      edit_workspace->property("cropToolMode").toString() !=
          QStringLiteral("transform") ||
      crop_tool_header_label->property("text").toString() != QStringLiteral("CROP / TRANSFORM") ||
      editor_preview.cropEditing() || crop_box_overlay->property("active").toBool() ||
      !crop_transform_overlay->property("active").toBool()) {
    return fail("crop transform mode should show an active transform surface and disable crop-box editing");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setCropToolMode",
          Q_ARG(QVariant, QVariant(QStringLiteral("horizon"))))) {
    return fail("edit workspace should expose horizon straighten submode switching");
  }
  QCoreApplication::processEvents();
  QVariant horizon_applied;
  constexpr auto kPi = 3.14159265358979323846;
  const auto expected_horizon_angle = std::atan2(0.2, 0.8) * 180.0 / kPi;
  if (edit_workspace->property("cropToolMode").toString() != QStringLiteral("horizon") ||
      crop_tool_header_label->property("text").toString() != QStringLiteral("CROP / HORIZON") ||
      editor_preview.cropEditing() || crop_box_overlay->property("active").toBool() ||
      crop_transform_overlay->property("active").toBool() ||
      !crop_horizon_overlay->property("active").toBool() ||
      !QMetaObject::invokeMethod(
          crop_horizon_overlay,
          "applyNormalizedGuide",
          Q_RETURN_ARG(QVariant, horizon_applied),
          Q_ARG(QVariant, 0.1),
          Q_ARG(QVariant, 0.3),
          Q_ARG(QVariant, 0.9),
          Q_ARG(QVariant, 0.5)) ||
      !horizon_applied.toBool() || std::abs(editor_preview.transformRotation() + expected_horizon_angle) > 1e-4) {
    std::cerr << "Horizon state: mode=" << edit_workspace->property("cropToolMode").toString().toStdString()
              << " cropEditing=" << editor_preview.cropEditing()
              << " cropActive=" << crop_box_overlay->property("active").toBool()
              << " transformActive=" << crop_transform_overlay->property("active").toBool()
              << " horizonActive=" << crop_horizon_overlay->property("active").toBool()
              << " applied=" << horizon_applied.toBool()
              << " rotation=" << editor_preview.transformRotation()
              << " expected=" << -expected_horizon_angle << '\n';
    return fail("crop horizon mode should expose a direct draggable guide that straightens from endpoints");
  }
  editor_preview.undo();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setCropToolMode",
          Q_ARG(QVariant, QVariant(QStringLiteral("crop"))))) {
    return fail("edit workspace should switch back to crop-box mode");
  }
  QCoreApplication::processEvents();
  if (edit_workspace->property("cropToolMode").toString() != QStringLiteral("crop") ||
      crop_tool_header_label->property("text").toString() != QStringLiteral("CROP / BOX") ||
      !editor_preview.cropEditing() || !crop_box_overlay->property("active").toBool() ||
      crop_transform_overlay->property("active").toBool()) {
    return fail("crop-box mode should reactivate direct crop editing and hide transform handles");
  }
  crop_box_overlay->setProperty("imageX", 0.0);
  crop_box_overlay->setProperty("imageY", 0.0);
  crop_box_overlay->setProperty("imageWidth", 200.0);
  crop_box_overlay->setProperty("imageHeight", 100.0);
  crop_box_overlay->setProperty("cropX", 0.25);
  crop_box_overlay->setProperty("cropY", 0.2);
  crop_box_overlay->setProperty("cropWidth", 0.5);
  crop_box_overlay->setProperty("cropHeight", 0.5);
  QCoreApplication::processEvents();
  QVariant near_corner_hit_mode;
  QVariant center_hit_mode;
  if (crop_box_overlay->property("handleHitSize").toDouble() < 28.0 ||
      crop_box_overlay->property("handleVisualSize").toDouble() > 16.0 ||
      !QMetaObject::invokeMethod(
          crop_box_overlay,
          "testHitMode",
          Q_RETURN_ARG(QVariant, near_corner_hit_mode),
          Q_ARG(QVariant, 39.0),
          Q_ARG(QVariant, 9.0)) ||
      near_corner_hit_mode.toString() != QStringLiteral("topLeft") ||
      !QMetaObject::invokeMethod(
          crop_box_overlay,
          "testHitMode",
          Q_RETURN_ARG(QVariant, center_hit_mode),
          Q_ARG(QVariant, 100.0),
          Q_ARG(QVariant, 45.0)) ||
      center_hit_mode.toString() != QStringLiteral("move")) {
    return fail("crop-box overlay should expose forgiving handle hit targets without turning the whole crop into a handle");
  }
  const QStringList guide_modes{
      QStringLiteral("Off"),
      QStringLiteral("Thirds"),
      QStringLiteral("Center"),
      QStringLiteral("Diagonal"),
      QStringLiteral("Golden Ratio"),
      QStringLiteral("Golden Spiral"),
  };
  if (crop_box_overlay->property("guideLineOpacity").toDouble() > 0.45 ||
      crop_box_overlay->property("guideSpiralOpacity").toDouble() > 0.48) {
    return fail("crop guide overlays should stay visually quiet enough not to overpower the image");
  }
  for (const auto& guide_mode : guide_modes) {
    edit_workspace->setProperty("cropGuideMode", guide_mode);
    QCoreApplication::processEvents();
    if (crop_box_overlay->property("guideMode").toString() != guide_mode) {
      return fail("crop guide selector should bind every guide mode to the on-canvas overlay");
    }
  }
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  editor_preview.setCropEditing(false);
  QCoreApplication::processEvents();

  const auto initial_mask_count = editor_preview.maskCount();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("intersect")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("linear"))))) {
    return fail("edit workspace should expose testable mask creation controls");
  }
  QCoreApplication::processEvents();
  auto* mask_creation_toolbar = edit_workspace->findChild<QObject*>(QStringLiteral("maskCreationToolbar"));
  auto* mask_intersect_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineIntersectButton"));
  auto* active_mask_toolbar_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeMaskToolbarStatusLabel"));
  auto* mask_stack_strip = find_named_object(edit_workspace, QStringLiteral("maskStackStrip"));
  auto* mask_stack_set_chip = find_named_object(edit_workspace, QStringLiteral("maskStackSetChip"));
  auto* mask_stack_set_label = find_named_object(edit_workspace, QStringLiteral("maskStackSetLabel"));
  auto* mask_stack_set_summary_label =
      find_named_object(edit_workspace, QStringLiteral("maskStackSetSummaryLabel"));
  auto* edit_mask_canvas_overlay =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportMaskCanvasOverlay"));
  auto* edit_generic_mask_tint =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportGenericMaskTint"));
  if (mask_creation_toolbar == nullptr || mask_intersect_button == nullptr ||
      active_mask_toolbar_status_label == nullptr ||
      mask_stack_strip == nullptr || mask_stack_set_chip == nullptr ||
      mask_stack_set_label == nullptr || mask_stack_set_summary_label == nullptr ||
      edit_mask_canvas_overlay == nullptr || edit_generic_mask_tint == nullptr ||
      !mask_creation_toolbar->property("visible").toBool() ||
      !mask_stack_strip->property("visible").toBool() ||
      !mask_stack_set_chip->property("visible").toBool() ||
      !mask_stack_set_label->property("text").toString().contains(QStringLiteral("Linear")) ||
      mask_stack_set_summary_label->property("text").toString() != QStringLiteral("1 source") ||
      !edit_mask_canvas_overlay->property("visible").toBool() ||
      edit_generic_mask_tint->property("visible").toBool() ||
      edit_generic_mask_tint->property("targetOpacity").toDouble() > 0.20 ||
      edit_mask_canvas_overlay->property("previewOverlayOpacity").toDouble() > 0.45 ||
      edit_mask_canvas_overlay->property("selectionOverlayOpacity").toDouble() > 0.68 ||
      edit_mask_canvas_overlay->property("geometricOverlayOpacity").toDouble() > 0.56 ||
      edit_mask_canvas_overlay->property("geometricFillOpacity").toDouble() > 0.12 ||
      edit_mask_canvas_overlay->property("geometricStrokeOpacity").toDouble() > 0.78 ||
      !mask_intersect_button->property("active").toBool() ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Linear source")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Intersect")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Normal")) ||
      editor_preview.maskCount() != initial_mask_count + 1 ||
      editor_preview.activeMaskOperation() != QStringLiteral("intersect") ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay")) {
    std::cerr << "Mask creation smoke state:"
              << " stackStrip=" << (mask_stack_strip == nullptr ? "null" : "present")
              << " stackVisible="
              << (mask_stack_strip != nullptr && mask_stack_strip->property("visible").toBool())
              << " chip=" << (mask_stack_set_chip == nullptr ? "null" : "present")
              << " chipVisible="
              << (mask_stack_set_chip != nullptr && mask_stack_set_chip->property("visible").toBool())
              << " chipThumb="
              << (mask_stack_set_chip == nullptr
                      ? std::string()
                      : mask_stack_set_chip->property("thumbnailSource").toString().left(24).toStdString())
              << " label="
              << (mask_stack_set_label == nullptr
                      ? std::string()
                      : mask_stack_set_label->property("text").toString().toStdString())
              << " summary="
              << (mask_stack_set_summary_label == nullptr
                      ? std::string()
                      : mask_stack_set_summary_label->property("text").toString().toStdString())
              << " overlay=" << (edit_mask_canvas_overlay == nullptr ? "null" : "present")
              << " overlayVisible="
              << (edit_mask_canvas_overlay != nullptr &&
                  edit_mask_canvas_overlay->property("visible").toBool())
              << " genericTintVisible="
              << (edit_generic_mask_tint != nullptr && edit_generic_mask_tint->property("visible").toBool())
              << " previewEmpty=" << editor_preview.activeMaskPreviewSource().isEmpty()
              << " toolbarText="
              << (active_mask_toolbar_status_label == nullptr
                      ? std::string()
                      : active_mask_toolbar_status_label->property("text").toString().toStdString())
              << " maskCount=" << editor_preview.maskCount()
              << " activeOp=" << editor_preview.activeMaskOperation().toStdString()
              << " previewMode=" << editor_preview.maskPreviewMode().toStdString() << "\n";
    return fail("mask creation should apply the selected combine mode and immediately show the mask in the stack and on canvas");
  }
  editor_preview.setActiveMaskInverted(true);
  QCoreApplication::processEvents();
  if (!active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Inverted"))) {
    return fail("mask toolbar should make active source inversion visible");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("subtract")))) ||
      !QMetaObject::invokeMethod(edit_workspace, "startMaskBrushSource")) {
    return fail("edit workspace should start brush masking through the shared mask creation controls");
  }
  QCoreApplication::processEvents();
  auto* mask_subtract_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineSubtractButton"));
  if (mask_subtract_button == nullptr || !mask_subtract_button->property("active").toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("brush") ||
      editor_preview.maskCount() != initial_mask_count + 2 ||
      editor_preview.activeMaskOperation() != QStringLiteral("subtract")) {
    return fail("brush mask creation should honor the selected subtract combine mode");
  }
  auto* brush_tool_hud = edit_workspace->findChild<QObject*>(QStringLiteral("brushToolHud"));
  auto* brush_add_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudAddButton"));
  auto* brush_erase_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudEraseButton"));
  auto* brush_status_label = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudStatusLabel"));
  auto* brush_size_up_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudSizeUpButton"));
  auto* brush_size_down_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushSizeDownShortcut"));
  auto* brush_size_up_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushSizeUpShortcut"));
  auto* brush_hardness_down_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHardnessDownShortcut"));
  auto* brush_hardness_up_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHardnessUpShortcut"));
  auto* brush_cursor_ring = edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportBrushCursorRing"));
  QVariant brush_ready;
  if (brush_tool_hud == nullptr || brush_add_button == nullptr || brush_erase_button == nullptr ||
      brush_status_label == nullptr || brush_size_up_button == nullptr ||
      brush_size_down_shortcut == nullptr || brush_size_up_shortcut == nullptr ||
      brush_hardness_down_shortcut == nullptr || brush_hardness_up_shortcut == nullptr ||
      brush_cursor_ring == nullptr ||
      !QMetaObject::invokeMethod(edit_workspace, "brushToolReady", Q_RETURN_ARG(QVariant, brush_ready)) ||
      !brush_ready.toBool() || !brush_tool_hud->property("visible").toBool() ||
      !brush_erase_button->property("active").toBool()) {
    return fail("active brush masking should show an on-canvas brush HUD, cursor, and shortcut set with erase mode selected");
  }
  const auto brush_size_before = editor_preview.activeMaskBrushSize();
  const auto brush_flow_before = editor_preview.activeMaskBrushFlow();
  const auto brush_hardness_before = editor_preview.activeMaskBrushHardness();
  QVariant brush_size_adjusted;
  QVariant brush_flow_adjusted;
  QVariant brush_hardness_adjusted;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushSize",
          Q_RETURN_ARG(QVariant, brush_size_adjusted),
          Q_ARG(QVariant, 0.02)) ||
      !brush_size_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushFlow",
          Q_RETURN_ARG(QVariant, brush_flow_adjusted),
          Q_ARG(QVariant, -0.05)) ||
      !brush_flow_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushHardness",
          Q_RETURN_ARG(QVariant, brush_hardness_adjusted),
          Q_ARG(QVariant, 0.08)) ||
      !brush_hardness_adjusted.toBool() ||
      editor_preview.activeMaskBrushSize() <= brush_size_before ||
      editor_preview.activeMaskBrushFlow() >= brush_flow_before ||
      editor_preview.activeMaskBrushHardness() <= brush_hardness_before) {
    return fail("brush HUD adjustment functions should update size, flow, and hardness");
  }
  QCoreApplication::processEvents();
  if (!brush_status_label->property("text").toString().contains(QStringLiteral("Size")) ||
      !brush_status_label->property("text").toString().contains(QStringLiteral("Flow")) ||
      !brush_status_label->property("text").toString().contains(QStringLiteral("Hard")) ||
      brush_cursor_ring->property("brushDiameter").toDouble() <= 0.0 ||
      brush_cursor_ring->property("hardnessDiameter").toDouble() <= 0.0 ||
      brush_cursor_ring->property("hardnessDiameter").toDouble() >
          brush_cursor_ring->property("brushDiameter").toDouble() ||
      brush_cursor_ring->property("brushModeLabel").toString() != QStringLiteral("Erase")) {
    return fail("brush HUD and cursor should expose true size, flow, hardness, and active mode");
  }
  editor_preview.beginActiveBrushStroke();
  editor_preview.addBrushPointWithTablet(0.42, 0.52, 0.64, 0.0, 0.0);
  QCoreApplication::processEvents();
  if (!brush_status_label->property("text").toString().contains(QStringLiteral("Pressure 64%")) ||
      brush_cursor_ring->property("pressureText").toString() != QStringLiteral("Pressure 64%")) {
    return fail("brush HUD and cursor should expose live tablet pressure while painting");
  }
  editor_preview.endActiveBrushStroke();
  QVariant brush_add_mode;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setBrushPaintMode",
          Q_RETURN_ARG(QVariant, brush_add_mode),
          Q_ARG(QVariant, QStringLiteral("add"))) ||
      brush_add_mode.toString() != QStringLiteral("add") ||
      editor_preview.activeMaskOperation() != QStringLiteral("add")) {
    return fail("brush HUD should switch the active brush between erase and add modes");
  }
  QCoreApplication::processEvents();
  if (!brush_add_button->property("active").toBool()) {
    return fail("brush HUD add button should reflect the active brush mode");
  }
  QVariant retouch_mode;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startRetouchTool",
          Q_RETURN_ARG(QVariant, retouch_mode),
          Q_ARG(QVariant, QStringLiteral("heal"))) ||
      retouch_mode.toString() != QStringLiteral("heal")) {
    return fail("edit workspace should expose a direct retouch starter");
  }
  QCoreApplication::processEvents();
  auto* retouch_tool_hud = edit_workspace->findChild<QObject*>(QStringLiteral("retouchToolHud"));
  auto* retouch_heal_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudHealButton"));
  auto* retouch_remove_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRemoveButton"));
  auto* retouch_clone_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudCloneButton"));
  auto* retouch_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudStatusLabel"));
  auto* retouch_regenerate_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRegenerateButton"));
  if (retouch_tool_hud == nullptr || retouch_heal_button == nullptr ||
      retouch_remove_button == nullptr || retouch_clone_button == nullptr ||
      retouch_status_label == nullptr || retouch_regenerate_button == nullptr ||
      !retouch_tool_hud->property("visible").toBool() ||
      !retouch_heal_button->property("active").toBool() ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Heal")) ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Paint area")) ||
      !retouch_regenerate_button->property("enabled").toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("retouch") ||
      editor_preview.activeMaskToolRole() != QStringLiteral("retouch") ||
      editor_preview.activeMaskRetouchMode() != QStringLiteral("heal")) {
    return fail("retouch workflow should show a dedicated HUD with the active heal mode");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startRetouchTool",
          Q_RETURN_ARG(QVariant, retouch_mode),
          Q_ARG(QVariant, QStringLiteral("remove"))) ||
      retouch_mode.toString() != QStringLiteral("remove")) {
    return fail("retouch HUD should switch between retouch modes");
  }
  QCoreApplication::processEvents();
  if (!retouch_remove_button->property("active").toBool() ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Remove")) ||
      editor_preview.activeMaskRetouchMode() != QStringLiteral("remove")) {
    return fail("retouch remove button should reflect the active retouch mode");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startRetouchTool",
          Q_RETURN_ARG(QVariant, retouch_mode),
          Q_ARG(QVariant, QStringLiteral("clone"))) ||
      retouch_mode.toString() != QStringLiteral("clone")) {
    return fail("retouch HUD should start clone stamp directly");
  }
  QCoreApplication::processEvents();
  if (!retouch_clone_button->property("active").toBool() ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Clone Stamp")) ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Pick source"))) {
    return fail("clone stamp HUD should clearly require a source before painting");
  }
  editor_preview.setActiveMaskCloneSource(0.34, 0.46);
  QCoreApplication::processEvents();
  auto* clone_source_pin =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneSourcePin"));
  auto* clone_source_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneSourceLabel"));
  if (clone_source_pin == nullptr || clone_source_label == nullptr ||
      !clone_source_pin->property("visible").toBool() ||
      clone_source_pin->property("sourceStatus").toString() != QStringLiteral("Source set") ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Source set"))) {
    return fail("clone stamp should show a movable source pin after picking a source");
  }
  if (!QMetaObject::invokeMethod(
          clone_source_pin,
          "moveSource",
          Q_ARG(QVariant, 0.62),
          Q_ARG(QVariant, 0.28)) ||
      std::abs(editor_preview.activeMaskCloneSourceX() - 0.62) > 1e-6 ||
      std::abs(editor_preview.activeMaskCloneSourceY() - 0.28) > 1e-6) {
    return fail("clone source pin should move the active clone source");
  }
  editor_preview.deselectActiveMask();
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* white_balance_panel_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("whiteBalancePanelResetButton"));
  auto* light_panel_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("lightPanelResetButton"));
  auto* color_panel_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("colorPanelResetButton"));
  if (white_balance_panel_reset_button == nullptr || light_panel_reset_button == nullptr ||
      color_panel_reset_button == nullptr || !white_balance_panel_reset_button->property("visible").toBool() ||
      !light_panel_reset_button->property("visible").toBool() ||
      !color_panel_reset_button->property("visible").toBool()) {
    return fail("adjust inspector should expose per-panel reset buttons for white balance, light, and color");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("detail"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* crop_rect_reset_button = edit_workspace->findChild<QObject*>(QStringLiteral("resetCropRectButton"));
  auto* transform_reset_button = edit_workspace->findChild<QObject*>(QStringLiteral("resetTransformButton"));
  auto* crop_aspect_combo = edit_workspace->findChild<QObject*>(QStringLiteral("cropAspectPresetComboBox"));
  auto* crop_aspect_input = edit_workspace->findChild<QObject*>(QStringLiteral("cropAspectPresetInput"));
  auto* crop_aspect_add_button = edit_workspace->findChild<QObject*>(QStringLiteral("addCropAspectPresetButton"));
  auto* crop_aspect_remove_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("removeCropAspectPresetButton"));
  if (crop_rect_reset_button == nullptr || transform_reset_button == nullptr || crop_aspect_combo == nullptr ||
      crop_aspect_input == nullptr || crop_aspect_add_button == nullptr || crop_aspect_remove_button == nullptr ||
      !crop_rect_reset_button->property("visible").toBool() ||
      !transform_reset_button->property("visible").toBool() ||
      crop_aspect_combo->property("count").toInt() < 6 ||
      !crop_aspect_combo->property("visible").toBool()) {
    return fail("detail inspector should expose separate crop/transform resets and a discoverable aspect preset menu");
  }
  editor_preview.removeCustomCropAspectPreset(QStringLiteral("13:7"));
  QVariant added_crop_aspect;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "addCropAspectPreset",
          Q_RETURN_ARG(QVariant, added_crop_aspect),
          Q_ARG(QVariant, QStringLiteral("13:7"))) ||
      !added_crop_aspect.toBool()) {
    return fail("studio crop inspector should add custom aspect presets through the shared controller API");
  }
  QCoreApplication::processEvents();
  if (editor_preview.cropPreset() != QStringLiteral("13:7") ||
      !editor_preview.cropAspectPresets().contains(QStringLiteral("13:7")) ||
      !crop_aspect_remove_button->property("enabled").toBool()) {
    return fail("new custom crop aspect presets should become selected and removable in the Studio inspector");
  }
  QVariant removed_crop_aspect;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "removeCropAspectPreset",
          Q_RETURN_ARG(QVariant, removed_crop_aspect),
          Q_ARG(QVariant, QStringLiteral("13:7"))) ||
      !removed_crop_aspect.toBool() || editor_preview.cropPreset() != QStringLiteral("Original") ||
      editor_preview.cropAspectPresets().contains(QStringLiteral("13:7"))) {
    return fail("studio crop inspector should remove custom aspect presets and return to Original when selected");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));

  if (!editor_preview.addFillLayer(QStringLiteral("#808080"), QStringLiteral("Smoke Fill")) ||
      !editor_preview.addTextLayer(QStringLiteral("Layer"), QStringLiteral("Smoke Text"))) {
    return fail("layer panel smoke setup should create editable layers");
  }
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* photo_layer_solo_button = edit_workspace->findChild<QObject*>(QStringLiteral("photoLayerSoloButton"));
  auto* photo_layer_clear_solo_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("photoLayerClearSoloButton"));
  if (photo_layer_solo_button == nullptr || photo_layer_clear_solo_button == nullptr ||
      photo_layer_solo_button->property("text").toString() != QStringLiteral("Isolate") ||
      photo_layer_clear_solo_button->property("visible").toBool()) {
    std::cerr << "Layer isolate smoke state: soloButton="
              << (photo_layer_solo_button == nullptr ? "null" : "present")
              << " text="
              << (photo_layer_solo_button == nullptr
                      ? std::string()
                      : photo_layer_solo_button->property("text").toString().toStdString())
              << " clearButton="
              << (photo_layer_clear_solo_button == nullptr ? "null" : "present")
              << " clearVisible="
              << (photo_layer_clear_solo_button != nullptr &&
                  photo_layer_clear_solo_button->property("visible").toBool())
              << " layerCount=" << editor_preview.photoLayerCount()
              << " soloActive=" << editor_preview.photoLayerSoloActive() << "\n";
    return fail("layer panel should expose an isolate control and hide clear isolation when inactive");
  }
  auto* photo_layer_opacity_label = find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityLabel"));
  auto* photo_layer_opacity_down_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityDownButton"));
  auto* photo_layer_opacity_up_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityUpButton"));
  auto* photo_layer_blend_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerBlendModeButton"));
  auto* photo_layer_clip_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerClippingMaskButton"));
  auto* photo_layer_mask_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskButton"));
  auto* photo_layer_mask_badge = find_named_object(edit_workspace, QStringLiteral("photoLayerMaskBadge"));
  auto* photo_layer_transform_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTransformButton"));
  if (photo_layer_opacity_label == nullptr || photo_layer_opacity_down_button == nullptr ||
      photo_layer_opacity_up_button == nullptr || photo_layer_blend_button == nullptr ||
      photo_layer_clip_button == nullptr || photo_layer_mask_button == nullptr ||
      photo_layer_mask_badge == nullptr || photo_layer_transform_button == nullptr) {
    std::cerr << "Layer row control presence: opacityLabel="
              << (photo_layer_opacity_label == nullptr ? "null" : "present")
              << " opacityDown=" << (photo_layer_opacity_down_button == nullptr ? "null" : "present")
              << " opacityUp=" << (photo_layer_opacity_up_button == nullptr ? "null" : "present")
              << " blend=" << (photo_layer_blend_button == nullptr ? "null" : "present")
              << " clip=" << (photo_layer_clip_button == nullptr ? "null" : "present")
              << " mask=" << (photo_layer_mask_button == nullptr ? "null" : "present")
              << " maskBadge=" << (photo_layer_mask_badge == nullptr ? "null" : "present")
              << " transform=" << (photo_layer_transform_button == nullptr ? "null" : "present")
              << " layerCount=" << editor_preview.photoLayerCount() << "\n";
    return fail("layer panel should expose opacity, blend, clipping, and layer mask controls per row");
  }
  auto* layer_visibility_state_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerVisibilityStateLabel"));
  auto* save_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("saveLayerVisibilityStateButton"));
  auto* apply_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("applyLayerVisibilityStateButton"));
  auto* delete_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("deleteLayerVisibilityStateButton"));
  if (layer_visibility_state_label == nullptr || save_layer_visibility_state_button == nullptr ||
      apply_layer_visibility_state_button == nullptr || delete_layer_visibility_state_button == nullptr ||
      layer_visibility_state_label->property("text").toString() != QStringLiteral("No visibility states") ||
      !save_layer_visibility_state_button->property("enabled").toBool() ||
      apply_layer_visibility_state_button->property("enabled").toBool() ||
      delete_layer_visibility_state_button->property("enabled").toBool()) {
    return fail("layer panel should expose disabled visibility-state apply/delete controls until a state exists");
  }
  if (!editor_preview.setPhotoLayerEnabled(0, false) ||
      !editor_preview.savePhotoLayerVisibilityState(QStringLiteral("Smoke Visibility"))) {
    return fail("layer panel smoke setup should save a layer visibility state");
  }
  for (auto iteration = 0; iteration < 4; ++iteration) {
    QCoreApplication::processEvents();
  }
  if (editor_preview.photoLayerVisibilityStateCount() != 1 ||
      !layer_visibility_state_label->property("text").toString().contains(QStringLiteral("Smoke Visibility")) ||
      !layer_visibility_state_label->property("text").toString().contains(QStringLiteral("1/2 visible")) ||
      !apply_layer_visibility_state_button->property("enabled").toBool() ||
      !delete_layer_visibility_state_button->property("enabled").toBool()) {
    return fail("layer panel should show the latest saved visibility state and enable restore/delete controls");
  }
  if (!editor_preview.setPhotoLayerEnabled(0, true) ||
      !editor_preview.applyPhotoLayerVisibilityState(0) ||
      editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("enabled")).toBool() ||
      !editor_preview.deletePhotoLayerVisibilityState(0) ||
      !editor_preview.setPhotoLayerEnabled(0, true)) {
    return fail("layer visibility state controls should restore, delete, and allow returning to the full stack");
  }
  QCoreApplication::processEvents();
  QVariant layer_transform_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayer",
          Q_RETURN_ARG(QVariant, layer_transform_selected),
          Q_ARG(QVariant, 0)) ||
      !layer_transform_selected.toBool()) {
    return fail("layer panel should expose a selectable direct-transform target");
  }
  for (auto iteration = 0; iteration < 4; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* photo_layer_transform_overlay =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTransformOverlay"));
  auto* photo_layer_transform_frame =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTransformFrame"));
  auto* photo_layer_move_handle =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTransformMoveHandle"));
  auto* photo_layer_rotate_handle =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTransformRotateHandle"));
  auto* photo_layer_scale_handle =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTransformBottomRightHandle"));
  auto* photo_layer_transform_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTransformLabel"));
  if (photo_layer_transform_overlay == nullptr || photo_layer_transform_frame == nullptr ||
      photo_layer_move_handle == nullptr || photo_layer_rotate_handle == nullptr ||
      photo_layer_scale_handle == nullptr || photo_layer_transform_label == nullptr ||
      !photo_layer_transform_overlay->property("active").toBool() ||
      !photo_layer_transform_overlay->property("visible").toBool() ||
      !photo_layer_transform_button->property("active").toBool()) {
    return fail("active photo layers should expose direct on-canvas transform handles");
  }
  const auto transform_scale_x =
      photo_layer_transform_overlay->property("viewportScaleX").toDouble();
  const auto transform_scale_y =
      photo_layer_transform_overlay->property("viewportScaleY").toDouble();
  const auto transform_start = editor_preview.photoLayers().at(0).toMap();
  QVariant transform_moved;
  const auto transform_move_invoked = QMetaObject::invokeMethod(
          photo_layer_transform_overlay,
          "moveByViewportDelta",
          Q_RETURN_ARG(QVariant, transform_moved),
          Q_ARG(QVariant, 24.0),
          Q_ARG(QVariant, 12.0));
  if (transform_scale_x <= 0.0 || transform_scale_y <= 0.0 ||
      !transform_move_invoked || !transform_moved.toBool()) {
    std::cerr << "Layer transform move state: scaleX=" << transform_scale_x
              << " scaleY=" << transform_scale_y
              << " invoked=" << transform_move_invoked
              << " returned=" << transform_moved.toBool()
              << " active=" << photo_layer_transform_overlay->property("active").toBool()
              << " visible=" << photo_layer_transform_overlay->property("visible").toBool()
              << " enabled=" << photo_layer_transform_overlay->property("enabled").toBool()
              << " locked=" << photo_layer_transform_overlay->property("locked").toBool()
              << "\n";
    return fail("layer transform overlay should expose a precise move operation");
  }
  QCoreApplication::processEvents();
  const auto moved_layer = editor_preview.photoLayers().at(0).toMap();
  const auto expected_layer_x =
      qRound(transform_start.value(QStringLiteral("x")).toDouble() + 24.0 / transform_scale_x);
  const auto expected_layer_y =
      qRound(transform_start.value(QStringLiteral("y")).toDouble() + 12.0 / transform_scale_y);
  if (moved_layer.value(QStringLiteral("x")).toInt() != expected_layer_x ||
      moved_layer.value(QStringLiteral("y")).toInt() != expected_layer_y ||
      !photo_layer_transform_label->property("text").toString().contains(QStringLiteral("X "))) {
    return fail("layer transform overlay movement should update exact layer transform metadata");
  }
  if (!editor_preview.setPhotoLayerOpacity(0, 0.42) ||
      !editor_preview.setPhotoLayerBlendMode(0, QStringLiteral("multiply")) ||
      !editor_preview.setPhotoLayerClippingMask(0, true) ||
      !editor_preview.setPhotoLayerMaskEnabled(0, true)) {
    return fail("layer panel smoke setup should update direct layer control state");
  }
  for (auto iteration = 0; iteration < 4; ++iteration) {
    QCoreApplication::processEvents();
  }
  if (photo_layer_opacity_label->property("text").toString() != QStringLiteral("42%") ||
      photo_layer_blend_button->property("text").toString() != QStringLiteral("Multiply") ||
      !photo_layer_blend_button->property("active").toBool() ||
      !photo_layer_clip_button->property("active").toBool() ||
      !photo_layer_mask_button->property("active").toBool() ||
      !photo_layer_mask_badge->property("visible").toBool()) {
    std::cerr << "Layer controls smoke state: opacity="
              << photo_layer_opacity_label->property("text").toString().toStdString()
              << " blend=" << photo_layer_blend_button->property("text").toString().toStdString()
              << " blendActive=" << photo_layer_blend_button->property("active").toBool()
              << " clipActive=" << photo_layer_clip_button->property("active").toBool()
              << " maskActive=" << photo_layer_mask_button->property("active").toBool()
              << " maskBadgeVisible=" << photo_layer_mask_badge->property("visible").toBool()
              << "\n";
    return fail("layer panel controls should reflect live opacity, blend, clipping, and mask states");
  }
  if (!editor_preview.isolatePhotoLayer(0)) {
    return fail("layer panel smoke setup should isolate the selected layer");
  }
  QCoreApplication::processEvents();
  if (!editor_preview.photoLayerSoloActive() ||
      photo_layer_solo_button->property("text").toString() != QStringLiteral("Isolated") ||
      !photo_layer_clear_solo_button->property("visible").toBool()) {
    return fail("layer panel should show clear isolation while a layer is isolated");
  }
  if (!editor_preview.clearPhotoLayerSolo()) {
    return fail("layer panel should clear isolation from the header control");
  }
  QCoreApplication::processEvents();

  auto expect_capability_registry = [&](const char* count_method,
                                        const char* signature_method,
                                        const QStringList& expected_capabilities,
                                        const char* registry_name,
                                        const char* workflow_name) -> int {
    QVariant capability_count;
    QVariant capability_signature;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            count_method,
            Q_RETURN_ARG(QVariant, capability_count)) ||
        capability_count.toInt() < expected_capabilities.size() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            signature_method,
            Q_RETURN_ARG(QVariant, capability_signature))) {
      std::cerr << "workspace should expose the " << registry_name << " capability registry\n";
      return EXIT_FAILURE;
    }
    const auto signature = capability_signature.toString();
    for (const auto& capability : expected_capabilities) {
      if (!signature.contains(capability)) {
        std::cerr << "workspace should expose every " << workflow_name
                  << " workflow capability\n";
        return EXIT_FAILURE;
      }
    }
    return EXIT_SUCCESS;
  };
  const auto edit_recipe_architecture_result =
      expect_capability_registry(
          "editRecipeArchitectureCapabilityCount",
          "editRecipeArchitectureCapabilitySignature",
          {
              QStringLiteral("unified-structured-recipe-model"),
          },
          "Edit Recipes",
          "Edit Recipes");
  if (edit_recipe_architecture_result != EXIT_SUCCESS) {
    return edit_recipe_architecture_result;
  }
  const auto color_science_capability_result =
      expect_capability_registry(
          "colorScienceCapabilityCount",
          "colorScienceCapabilitySignature",
          {
              QStringLiteral("icc-grade-color-management-layer"),
              QStringLiteral("ocio-style-configuration"),
          },
          "Color Science",
          "Color Science");
  if (color_science_capability_result != EXIT_SUCCESS) {
    return color_science_capability_result;
  }
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  edit_workspace->setProperty("loupeEnabled", true);
  edit_workspace->setProperty("loupeX", 96.0);
  edit_workspace->setProperty("loupeY", 72.0);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* loupe_view = edit_workspace->findChild<QObject*>(QStringLiteral("loupeView"));
  if (loupe_view == nullptr || !edit_workspace->property("loupeEnabled").toBool() ||
      edit_workspace->property("loupeX").toDouble() < 95.9 ||
      edit_workspace->property("loupeY").toDouble() < 71.9) {
    return fail("edit workspace should expose a loupe view for close inspection");
  }
  edit_workspace->setProperty("focusCheckOverlayEnabled", true);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* focus_check_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("focusCheckOverlay"));
  if (focus_check_overlay == nullptr ||
      !edit_workspace->property("focusCheckOverlayEnabled").toBool()) {
    return fail("edit workspace should expose a focus-check overlay");
  }
  edit_workspace->setProperty("focusPeakingOverlayEnabled", true);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* focus_peaking_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("focusPeakingOverlay"));
  if (focus_peaking_overlay == nullptr ||
      !edit_workspace->property("focusPeakingOverlayEnabled").toBool()) {
    return fail("edit workspace should expose focus peaking when source data supports it");
  }
  edit_workspace->setProperty("clippingOverlayEnabled", true);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* clipping_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("clippingOverlay"));
  if (clipping_overlay == nullptr || !edit_workspace->property("clippingOverlayEnabled").toBool()) {
    return fail("edit workspace should expose highlight and shadow clipping overlays");
  }
  edit_workspace->setProperty("falseColorOverlayEnabled", true);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* false_color_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("falseColorOverlay"));
  if (false_color_overlay == nullptr ||
      !edit_workspace->property("falseColorOverlayEnabled").toBool()) {
    return fail("edit workspace should expose a false-color overlay");
  }
  edit_workspace->setProperty("gamutOverlayEnabled", true);
  edit_workspace->setProperty("zoneMapOverlayEnabled", true);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* gamut_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("gamutOverlay"));
  auto* zone_map_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("zoneMapOverlay"));
  auto* clipping_overlay_target_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("clippingOverlayTargetLabel"));
  auto* gamut_overlay_target_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("gamutOverlayTargetLabel"));
  auto* pixel_inspector_hud = edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorHud"));
  auto* pixel_inspector_before_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorBeforeLabel"));
  auto* pixel_inspector_after_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorAfterLabel"));
  auto* pixel_inspector_delta_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorDeltaLabel"));
  if (gamut_overlay == nullptr || zone_map_overlay == nullptr ||
      !edit_workspace->property("gamutOverlayEnabled").toBool() ||
      !edit_workspace->property("zoneMapOverlayEnabled").toBool()) {
    return fail("edit workspace should expose gamut and zone-map analysis overlays");
  }
  if (clipping_overlay_target_label == nullptr || gamut_overlay_target_label == nullptr ||
      !clipping_overlay_target_label->property("text").toString().contains(QStringLiteral("Preview clipping")) ||
      !gamut_overlay_target_label->property("text").toString().contains(QStringLiteral("Output gamut"))) {
    return fail("analysis overlays should label the clipping and gamut target they refer to");
  }
  if (pixel_inspector_hud == nullptr || pixel_inspector_before_label == nullptr ||
      pixel_inspector_after_label == nullptr || pixel_inspector_delta_label == nullptr) {
    return fail("edit workspace should expose a before/after pixel inspector HUD");
  }
  edit_workspace->setProperty("editTool", QStringLiteral("compare"));
  edit_workspace->setProperty("compareViewMode", QStringLiteral("split"));
  edit_workspace->setProperty("compareSplitPosition", 0.35);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* before_after_split = edit_workspace->findChild<QObject*>(QStringLiteral("beforeAfterSplitView"));
  auto* focused_compare_panel =
      find_named_object(edit_workspace, QStringLiteral("beforeAfterSplitPanel"));
  if (!editor_preview.compareMode() ||
      edit_workspace->property("compareViewMode").toString() != QStringLiteral("split") ||
      edit_workspace->property("compareSplitPosition").toDouble() < 0.34 ||
      before_after_split == nullptr || focused_compare_panel == nullptr ||
      !focused_compare_panel->property("visible").toBool() ||
      edit_workspace->property("compareViewportCount").toInt() != 0) {
    return fail("split compare should dedicate the compare workspace to the before/after view");
  }
  edit_workspace->setProperty("compareViewMode", QStringLiteral("wipe"));
  edit_workspace->setProperty("compareSplitPosition", 0.65);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* before_after_wipe = edit_workspace->findChild<QObject*>(QStringLiteral("beforeAfterWipeView"));
  if (edit_workspace->property("compareViewMode").toString() != QStringLiteral("wipe") ||
      edit_workspace->property("compareSplitPosition").toDouble() < 0.64 ||
      before_after_wipe == nullptr || !focused_compare_panel->property("visible").toBool() ||
      edit_workspace->property("compareViewportCount").toInt() != 0) {
    return fail("wipe compare should keep the focused before/after view without extra columns");
  }
  edit_workspace->setProperty("compareViewMode", QStringLiteral("side-by-side"));
  edit_workspace->setProperty("syncEnabled", true);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  if (edit_workspace->property("compareViewportCount").toInt() != 2 ||
      focused_compare_panel->property("visible").toBool()) {
    return fail("side-by-side compare should expose only synchronized compare image viewports");
  }
  edit_workspace->setProperty("synchronizedZoom", 2.0);
  edit_workspace->setProperty("synchronizedPanX", 18.0);
  edit_workspace->setProperty("synchronizedPanY", -9.0);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  const auto synced_zoom = edit_workspace->property("synchronizedZoom").toDouble();
  const auto synced_pan_x = edit_workspace->property("synchronizedPanX").toDouble();
  const auto synced_pan_y = edit_workspace->property("synchronizedPanY").toDouble();
  if (synced_zoom < 1.99 || synced_zoom > 2.01 || synced_pan_x < 17.9 ||
      synced_pan_x > 18.1 || synced_pan_y > -8.9 || synced_pan_y < -9.1) {
    return fail("compare viewports should synchronize zoom and pan");
  }
  edit_workspace->setProperty("librarySourcesPanelVisible", false);
  edit_workspace->setProperty("libraryInspectorPanelVisible", false);
  edit_workspace->setProperty("editContextPanelVisible", false);
  edit_workspace->setProperty("editInspectorPanelVisible", false);
  edit_workspace->setProperty("exportPresetsPanelVisible", false);
  edit_workspace->setProperty("exportSettingsPanelVisible", false);
  QVariant panel_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "customizablePanelSignature",
          Q_RETURN_ARG(QVariant, panel_signature)) ||
      panel_signature.toString() != QStringLiteral("false|false|false|false|false|false")) {
    return fail("workspace panels should expose customizable visibility state");
  }
  if (!QMetaObject::invokeMethod(edit_workspace, "resetCustomizablePanels")) {
    return fail("workspace panels should expose a reset action");
  }
  QCoreApplication::processEvents();
  QVariant reset_panel_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "customizablePanelSignature",
          Q_RETURN_ARG(QVariant, reset_panel_signature)) ||
      reset_panel_signature.toString() != QStringLiteral("true|true|true|true|true|true")) {
    return fail("workspace panel customization reset should restore default panels");
  }
  edit_workspace->setProperty("librarySourcesPanelVisible", false);
  edit_workspace->setProperty("editContextPanelVisible", false);
  edit_workspace->setProperty("editInspectorPanelVisible", false);
  QVariant save_layout_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "saveWorkspaceLayout",
          Q_RETURN_ARG(QVariant, save_layout_result),
          Q_ARG(QVariant, QStringLiteral("Review Focus"))) ||
      !save_layout_result.toBool() || edit_workspace->property("savedWorkspaceLayoutCount").toInt() < 3) {
    return fail("workspace layout presets should save named panel arrangements");
  }
  if (!QMetaObject::invokeMethod(edit_workspace, "resetCustomizablePanels")) {
    return fail("workspace layout presets should allow reset before restore");
  }
  QVariant apply_layout_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyWorkspaceLayout",
          Q_RETURN_ARG(QVariant, apply_layout_result),
          Q_ARG(QVariant, QStringLiteral("Review Focus"))) ||
      !apply_layout_result.toBool()) {
    return fail("workspace layout presets should restore named panel arrangements");
  }
  QVariant restored_layout_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "customizablePanelSignature",
          Q_RETURN_ARG(QVariant, restored_layout_signature)) ||
      restored_layout_signature.toString() != QStringLiteral("false|true|false|false|true|true")) {
    return fail("workspace layout presets should restore saved panel visibility");
  }
  QVariant compact_mode_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setInterfaceMode",
          Q_RETURN_ARG(QVariant, compact_mode_result),
          Q_ARG(QVariant, QStringLiteral("compact"))) ||
      !compact_mode_result.toBool() ||
      edit_workspace->property("interfaceMode").toString() != QStringLiteral("compact") ||
      edit_workspace->property("editInspectorPanelWidth").toInt() >= 420) {
    return fail("workspace should expose compact panel density mode");
  }
  QVariant pro_mode_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setInterfaceMode",
          Q_RETURN_ARG(QVariant, pro_mode_result),
          Q_ARG(QVariant, QStringLiteral("pro"))) ||
      !pro_mode_result.toBool() ||
      edit_workspace->property("interfaceMode").toString() != QStringLiteral("pro") ||
      edit_workspace->property("editInspectorPanelWidth").toInt() < 420) {
    return fail("workspace should expose pro panel density mode");
  }
  edit_workspace->setProperty("editContextPanelVisible", true);
  edit_workspace->setProperty("editSidebarCollapsed", false);
  edit_workspace->setProperty("largeScopesMode", false);
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("compareViewMode", QStringLiteral("split"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QStringLiteral("compare")))) {
    return fail("edit workspace should expose compare activation");
  }
  QCoreApplication::processEvents();
  auto* edit_context_panel = edit_workspace->findChild<QObject*>(QStringLiteral("editContextPanel"));
  auto* before_after_split_panel = find_named_object(edit_workspace, QStringLiteral("beforeAfterSplitPanel"));
  if (edit_context_panel == nullptr || before_after_split_panel == nullptr ||
      edit_workspace->property("editContextPanelActive").toBool() ||
      edit_context_panel->property("visible").toBool() ||
      !before_after_split_panel->property("visible").toBool()) {
    return fail("compare view should hide the context rail and dedicate the canvas to before/after panels");
  }
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  QCoreApplication::processEvents();
  if (!edit_workspace->property("editContextPanelActive").toBool() ||
      !edit_context_panel->property("visible").toBool() ||
      edit_context_panel->property("title").toString() != QStringLiteral("Asset Context")) {
    return fail("adjust view should restore the context rail with readable panel chrome");
  }
  auto* edit_image_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
  auto* edit_zoom_toolbar = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
  auto* edit_action_toolbar = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
  auto* loupe_overlay_button = find_named_object(edit_workspace, QStringLiteral("loupeOverlayButton"));
  auto* gamut_overlay_button = find_named_object(edit_workspace, QStringLiteral("gamutOverlayButton"));
  if (edit_image_viewport == nullptr || edit_zoom_toolbar == nullptr || edit_action_toolbar == nullptr ||
      loupe_overlay_button == nullptr || gamut_overlay_button == nullptr) {
    return fail("edit canvas should expose responsive viewport toolbar controls");
  }
  const auto viewport_width = edit_image_viewport->property("width").toDouble();
  const auto toolbar_compact_width =
      edit_image_viewport->property("toolbarCompactWidth").toDouble();
  const auto toolbar_compact = viewport_width < toolbar_compact_width;
  const auto zoom_right =
      edit_zoom_toolbar->property("x").toDouble() + edit_zoom_toolbar->property("width").toDouble();
  const auto action_left = edit_action_toolbar->property("x").toDouble();
  const auto zoom_bottom =
      edit_zoom_toolbar->property("y").toDouble() + edit_zoom_toolbar->property("height").toDouble();
  const auto action_top = edit_action_toolbar->property("y").toDouble();
  const auto toolbar_stacked = edit_image_viewport->property("toolbarStacked").toBool();
  if (edit_image_viewport->property("toolbarCompact").toBool() != toolbar_compact ||
      loupe_overlay_button->property("labelVisible").toBool() == toolbar_compact ||
      gamut_overlay_button->property("tooltip").toString().isEmpty() ||
      (!toolbar_stacked && zoom_right + 8.0 > action_left) ||
      (toolbar_stacked && zoom_bottom + 6.0 > action_top)) {
    return fail("edit canvas toolbars should compact predictably and avoid overlap");
  }
  QVariant full_screen_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setFullScreenEditing",
          Q_RETURN_ARG(QVariant, full_screen_result),
          Q_ARG(QVariant, true)) ||
      !full_screen_result.toBool() ||
      !edit_workspace->property("fullScreenEditingEnabled").toBool() ||
      edit_workspace->property("editingChromeVisible").toBool()) {
    return fail("edit workspace should expose a full-screen editing mode");
  }
  QVariant exit_full_screen_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setFullScreenEditing",
          Q_RETURN_ARG(QVariant, exit_full_screen_result),
          Q_ARG(QVariant, false)) ||
      exit_full_screen_result.toBool() ||
      edit_workspace->property("fullScreenEditingEnabled").toBool() ||
      !edit_workspace->property("editingChromeVisible").toBool()) {
    return fail("edit workspace should restore chrome after full-screen editing");
  }
  QVariant narrow_layout_result;
  QVariant narrow_layout_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyResponsivePanelBehavior",
          Q_RETURN_ARG(QVariant, narrow_layout_result),
          Q_ARG(QVariant, 980)) ||
      !narrow_layout_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelSignature",
          Q_RETURN_ARG(QVariant, narrow_layout_signature)) ||
      narrow_layout_signature.toString() !=
          QStringLiteral("compact|true|false|false|true|false|false|false|false")) {
    return fail("workspace should collapse panels for small displays");
  }
  QVariant wide_layout_result;
  QVariant wide_layout_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyResponsivePanelBehavior",
          Q_RETURN_ARG(QVariant, wide_layout_result),
          Q_ARG(QVariant, 1440)) ||
      !wide_layout_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelSignature",
          Q_RETURN_ARG(QVariant, wide_layout_signature)) ||
      wide_layout_signature.toString() !=
          QStringLiteral("pro|false|true|true|false|true|true|true|true")) {
    return fail("workspace should restore panels after leaving small display mode");
  }
  catalog_assets.replaceAssetsForTesting({});
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  QVariant edit_empty_title;
  QVariant library_empty_title_from_edit_workspace;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editEmptyStateTitle",
          Q_RETURN_ARG(QVariant, edit_empty_title)) ||
      edit_empty_title.toString() != QStringLiteral("No Photo Selected") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "libraryEmptyStateTitle",
          Q_RETURN_ARG(QVariant, library_empty_title_from_edit_workspace)) ||
      library_empty_title_from_edit_workspace.toString().trimmed().isEmpty()) {
    return fail("workspace should expose clear empty and error state messages");
  }
  QVariant library_capability_count;
  QVariant library_capability_signature;
  const QStringList expected_library_capabilities = {
      QStringLiteral("albums"),
      QStringLiteral("collections"),
      QStringLiteral("smart-albums"),
      QStringLiteral("stacks"),
      QStringLiteral("versions"),
      QStringLiteral("virtual-copies"),
      QStringLiteral("duplicate-detection"),
      QStringLiteral("near-duplicate-clustering"),
      QStringLiteral("offline-media"),
      QStringLiteral("relink"),
      QStringLiteral("sidecars"),
      QStringLiteral("card-ingest"),
      QStringLiteral("checksum"),
      QStringLiteral("backup-copy"),
      QStringLiteral("rename-templates"),
      QStringLiteral("folder-templates"),
      QStringLiteral("metadata-preset-import"),
      QStringLiteral("develop-preset-import"),
      QStringLiteral("duplicate-avoidance-import"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "libraryCapabilityCount",
          Q_RETURN_ARG(QVariant, library_capability_count)) ||
      library_capability_count.toInt() < expected_library_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "libraryCapabilitySignature",
          Q_RETURN_ARG(QVariant, library_capability_signature))) {
    return fail("workspace should expose the Library and Catalog capability registry");
  }
  for (const auto& capability : expected_library_capabilities) {
    if (!library_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Library and Catalog workflow capability");
    }
  }
  QVariant metadata_capability_count;
  QVariant metadata_capability_signature;
  const QStringList expected_metadata_capabilities = {
      QStringLiteral("exif-iptc-xmp"),
      QStringLiteral("captions"),
      QStringLiteral("keywords"),
      QStringLiteral("people-tagging"),
      QStringLiteral("gps-map"),
      QStringLiteral("copyright-creator"),
      QStringLiteral("metadata-templates"),
      QStringLiteral("metadata-sync"),
      QStringLiteral("metadata-conflicts"),
      QStringLiteral("sidecar-round-trip"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "metadataCapabilityCount",
          Q_RETURN_ARG(QVariant, metadata_capability_count)) ||
      metadata_capability_count.toInt() < expected_metadata_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "metadataCapabilitySignature",
          Q_RETURN_ARG(QVariant, metadata_capability_signature))) {
    return fail("workspace should expose the Metadata capability registry");
  }
  for (const auto& capability : expected_metadata_capabilities) {
    if (!metadata_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Metadata workflow capability");
    }
  }
  QVariant culling_search_capability_count;
  QVariant culling_search_capability_signature;
  const QStringList expected_culling_search_capabilities = {
      QStringLiteral("embedded-jpeg-previews"),
      QStringLiteral("reject-flag-rating-label-hotkeys"),
      QStringLiteral("auto-advance"),
      QStringLiteral("burst-grouping"),
      QStringLiteral("pick-groups"),
      QStringLiteral("fast-compare-candidates"),
      QStringLiteral("focus-check-zoom"),
      QStringLiteral("rich-filtering"),
      QStringLiteral("saved-searches"),
      QStringLiteral("smart-filters"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "cullingSearchCapabilityCount",
          Q_RETURN_ARG(QVariant, culling_search_capability_count)) ||
      culling_search_capability_count.toInt() < expected_culling_search_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cullingSearchCapabilitySignature",
          Q_RETURN_ARG(QVariant, culling_search_capability_signature))) {
    return fail("workspace should expose the Culling and Search capability registry");
  }
  for (const auto& capability : expected_culling_search_capabilities) {
    if (!culling_search_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Culling and Search workflow capability");
    }
  }
  QVariant batch_editing_capability_count;
  QVariant batch_editing_capability_signature;
  const QStringList expected_batch_editing_capabilities = {
      QStringLiteral("selective-sync"),
      QStringLiteral("copy-paste-group-selection"),
      QStringLiteral("paste-conflict-preview"),
      QStringLiteral("match-exposure"),
      QStringLiteral("match-white-balance"),
      QStringLiteral("batch-crop"),
      QStringLiteral("batch-mask-ai"),
      QStringLiteral("batch-retouch-safe"),
      QStringLiteral("batch-metadata-templates"),
      QStringLiteral("batch-export-validation"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "batchEditingCapabilityCount",
          Q_RETURN_ARG(QVariant, batch_editing_capability_count)) ||
      batch_editing_capability_count.toInt() < expected_batch_editing_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "batchEditingCapabilitySignature",
          Q_RETURN_ARG(QVariant, batch_editing_capability_signature))) {
    return fail("workspace should expose the Batch Editing capability registry");
  }
  for (const auto& capability : expected_batch_editing_capabilities) {
    if (!batch_editing_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Batch Editing workflow capability");
    }
  }
  QVariant export_delivery_capability_count;
  QVariant export_delivery_capability_signature;
  const QStringList expected_export_delivery_capabilities = {
      QStringLiteral("custom-export-presets"),
      QStringLiteral("export-formats"),
      QStringLiteral("bit-depth-options"),
      QStringLiteral("color-profile-selection"),
      QStringLiteral("profile-embedding"),
      QStringLiteral("resize-modes"),
      QStringLiteral("output-sharpening"),
      QStringLiteral("watermarking"),
      QStringLiteral("metadata-export-controls"),
      QStringLiteral("filename-templates"),
      QStringLiteral("subfolder-templates"),
      QStringLiteral("collision-handling"),
      QStringLiteral("export-validation-warnings"),
      QStringLiteral("export-queue-controls"),
      QStringLiteral("export-manifests"),
      QStringLiteral("print-workflow"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "exportDeliveryCapabilityCount",
          Q_RETURN_ARG(QVariant, export_delivery_capability_count)) ||
      export_delivery_capability_count.toInt() < expected_export_delivery_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "exportDeliveryCapabilitySignature",
          Q_RETURN_ARG(QVariant, export_delivery_capability_signature))) {
    return fail("workspace should expose the Export and Delivery capability registry");
  }
  for (const auto& capability : expected_export_delivery_capabilities) {
    if (!export_delivery_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Export and Delivery workflow capability");
    }
  }
  QVariant performance_capability_count;
  QVariant performance_capability_signature;
  const QStringList expected_performance_capabilities = {
      QStringLiteral("tiled-rendering"),
      QStringLiteral("mip-pyramid-previews"),
      QStringLiteral("background-cache-warming"),
      QStringLiteral("viewport-only-rendering"),
      QStringLiteral("dirty-tile-rendering"),
      QStringLiteral("progressive-refinement"),
      QStringLiteral("gpu-pipeline-coverage"),
      QStringLiteral("cross-platform-gpu-abstraction"),
      QStringLiteral("windows-gpu-parity"),
      QStringLiteral("cpu-gpu-memory-budgets"),
      QStringLiteral("disk-cache-eviction"),
      QStringLiteral("gpu-texture-pool"),
      QStringLiteral("low-memory-recovery"),
      QStringLiteral("huge-file-panorama"),
      QStringLiteral("slider-latency-targets"),
      QStringLiteral("brush-latency-targets"),
      QStringLiteral("import-throughput-targets"),
      QStringLiteral("preview-render-targets"),
      QStringLiteral("export-throughput-targets"),
      QStringLiteral("benchmark-dashboards"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "performanceCapabilityCount",
          Q_RETURN_ARG(QVariant, performance_capability_count)) ||
      performance_capability_count.toInt() < expected_performance_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "performanceCapabilitySignature",
          Q_RETURN_ARG(QVariant, performance_capability_signature))) {
    return fail("workspace should expose the Performance capability registry");
  }
  for (const auto& capability : expected_performance_capabilities) {
    if (!performance_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Performance workflow capability");
    }
  }
  QVariant ai_model_runtime_capability_count;
  QVariant ai_model_runtime_capability_signature;
  const QStringList expected_ai_model_runtime_capabilities = {
      QStringLiteral("model-download-install-ui"),
      QStringLiteral("model-versioning"),
      QStringLiteral("acceleration-detection"),
      QStringLiteral("model-fallback-modes"),
      QStringLiteral("privacy-offline-indicators"),
      QStringLiteral("model-error-states"),
      QStringLiteral("model-device-selection"),
      QStringLiteral("quantized-model-options"),
      QStringLiteral("batch-inference"),
      QStringLiteral("preview-full-refinement"),
      QStringLiteral("model-warmup"),
      QStringLiteral("model-cache-management"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "aiModelRuntimeCapabilityCount",
          Q_RETURN_ARG(QVariant, ai_model_runtime_capability_count)) ||
      ai_model_runtime_capability_count.toInt() <
          expected_ai_model_runtime_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "aiModelRuntimeCapabilitySignature",
          Q_RETURN_ARG(QVariant, ai_model_runtime_capability_signature))) {
    return fail("workspace should expose the AI and Model Runtime capability registry");
  }
  for (const auto& capability : expected_ai_model_runtime_capabilities) {
    if (!ai_model_runtime_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every AI and Model Runtime workflow capability");
    }
  }
  const auto remaining_capability_result =
      expect_capability_registry(
          "reliabilityRecoveryCapabilityCount",
          "reliabilityRecoveryCapabilitySignature",
          {
              QStringLiteral("undo-redo-hardening"),
              QStringLiteral("visible-history-panel"),
              QStringLiteral("workspace-autosave"),
              QStringLiteral("crash-safe-recipe-writes"),
              QStringLiteral("job-recovery"),
              QStringLiteral("catalog-repair-tools"),
              QStringLiteral("cache-repair-tools"),
              QStringLiteral("relink-repair-tools"),
              QStringLiteral("project-package-collect-originals"),
              QStringLiteral("sidecar-backup-restore"),
          },
          "Reliability and Recovery",
          "Reliability and Recovery");
  if (remaining_capability_result != EXIT_SUCCESS) {
    return remaining_capability_result;
  }
  const auto collaboration_capability_result =
      expect_capability_registry(
          "collaborationHandoffCapabilityCount",
          "collaborationHandoffCapabilitySignature",
          {
              QStringLiteral("project-packaging"),
              QStringLiteral("collect-originals"),
              QStringLiteral("sidecar-recipes"),
              QStringLiteral("edit-history-export"),
              QStringLiteral("comments-annotations"),
              QStringLiteral("client-proof-galleries"),
              QStringLiteral("external-editor-roundtrip"),
              QStringLiteral("plugin-extension-api"),
              QStringLiteral("scripting-automation-hooks"),
              QStringLiteral("custom-export-actions"),
              QStringLiteral("custom-model-plugins"),
          },
          "Collaboration and Handoff",
          "Collaboration and Handoff");
  if (collaboration_capability_result != EXIT_SUCCESS) {
    return collaboration_capability_result;
  }
  const auto qa_capability_result =
      expect_capability_registry(
          "qaTestingCapabilityCount",
          "qaTestingCapabilitySignature",
          {
              QStringLiteral("golden-image-corpus"),
              QStringLiteral("raw-corpus"),
              QStringLiteral("color-chart-tests"),
              QStringLiteral("perceptual-delta-tests"),
              QStringLiteral("recipe-parser-fuzzing"),
              QStringLiteral("huge-file-stress-tests"),
              QStringLiteral("masks-layers-stress-tests"),
              QStringLiteral("catalog-import-stress-tests"),
              QStringLiteral("export-queue-stress-tests"),
              QStringLiteral("render-performance-benchmarks"),
              QStringLiteral("ui-smoke-tests-core-workflows"),
              QStringLiteral("model-runtime-tests-matrix"),
          },
          "QA and Testing",
          "QA and Testing");
  if (qa_capability_result != EXIT_SUCCESS) {
    return qa_capability_result;
  }
  const auto observability_capability_result =
      expect_capability_registry(
          "observabilityCapabilityCount",
          "observabilityCapabilitySignature",
          {
              QStringLiteral("render-timings"),
              QStringLiteral("gpu-fallback-reasons"),
              QStringLiteral("failed-exports"),
              QStringLiteral("failed-imports"),
              QStringLiteral("model-failures"),
              QStringLiteral("catalog-db-health"),
              QStringLiteral("cache-hit-rates"),
              QStringLiteral("memory-pressure"),
              QStringLiteral("privacy-safe-diagnostics-bundles"),
              QStringLiteral("crash-reporting"),
          },
          "Observability",
          "Observability");
  if (observability_capability_result != EXIT_SUCCESS) {
    return observability_capability_result;
  }
  const auto packaging_capability_result =
      expect_capability_registry(
          "packagingProductizationCapabilityCount",
          "packagingProductizationCapabilitySignature",
          {
              QStringLiteral("signed-installers"),
              QStringLiteral("macos-notarization"),
              QStringLiteral("update-channels"),
              QStringLiteral("migration-tests"),
              QStringLiteral("first-run-model-profile-setup"),
              QStringLiteral("sample-catalog"),
              QStringLiteral("reset-repair-tools"),
              QStringLiteral("licensing-entitlement-groundwork"),
              QStringLiteral("release-notes-workflow"),
          },
          "Packaging and Productization",
          "Packaging and Productization");
  if (packaging_capability_result != EXIT_SUCCESS) {
    return packaging_capability_result;
  }
  const auto accessibility_capability_result =
      expect_capability_registry(
          "accessibilityLocalizationCapabilityCount",
          "accessibilityLocalizationCapabilitySignature",
          {
              QStringLiteral("keyboard-only-editing"),
              QStringLiteral("screen-reader-labels"),
              QStringLiteral("scalable-ui"),
              QStringLiteral("high-contrast-mode"),
              QStringLiteral("reduced-motion-mode"),
              QStringLiteral("color-blind-safe-overlays"),
              QStringLiteral("visible-string-localization"),
              QStringLiteral("localized-metadata-date-number-formatting"),
          },
          "Accessibility and Localization",
          "Accessibility and Localization");
  if (accessibility_capability_result != EXIT_SUCCESS) {
    return accessibility_capability_result;
  }
  const auto documentation_capability_result =
      expect_capability_registry(
          "documentationHelpCapabilityCount",
          "documentationHelpCapabilitySignature",
          {
              QStringLiteral("searchable-command-help"),
              QStringLiteral("in-app-tool-help"),
              QStringLiteral("troubleshooting-diagnostics"),
              QStringLiteral("preset-locations-docs"),
              QStringLiteral("model-locations-docs"),
              QStringLiteral("profile-locations-docs"),
              QStringLiteral("sidecar-behavior-docs"),
              QStringLiteral("catalog-backup-recovery-docs"),
              QStringLiteral("export-color-management-docs"),
          },
          "Documentation and Help",
          "Documentation and Help");
  if (documentation_capability_result != EXIT_SUCCESS) {
    return documentation_capability_result;
  }
  const auto acceptance_criteria_result =
      expect_capability_registry(
          "acceptanceCriteriaCount",
          "acceptanceCriteriaSignature",
          {
              QStringLiteral("pro-shoot-end-to-end"),
              QStringLiteral("raw-color-quality"),
              QStringLiteral("nondestructive-recoverable-portable"),
              QStringLiteral("fast-precise-stack-masking"),
              QStringLiteral("credible-inpaint-retouch-undo"),
              QStringLiteral("layer-compositing-workflows"),
              QStringLiteral("responsive-large-workloads"),
              QStringLiteral("export-fidelity"),
              QStringLiteral("clear-failure-recovery"),
              QStringLiteral("professional-accessible-ui"),
          },
          "Acceptance Criteria",
          "Acceptance Criteria");
  if (acceptance_criteria_result != EXIT_SUCCESS) {
    return acceptance_criteria_result;
  }
  QVariant focus_peaking_tooltip;
  if (edit_workspace->property("complexTooltipCount").toInt() < 12 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "complexToolTooltip",
          Q_RETURN_ARG(QVariant, focus_peaking_tooltip),
          Q_ARG(QVariant, QStringLiteral("focus-peaking"))) ||
      !focus_peaking_tooltip.toString().contains(QStringLiteral("edges")) ||
      focus_peaking_tooltip.toString().size() < 80) {
    return fail("workspace should expose high-quality tooltips for complex tools");
  }
  QVariant health_signature;
  QVariant health_count;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "systemHealthSignature",
          Q_RETURN_ARG(QVariant, health_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "systemHealthItemCount",
          Q_RETURN_ARG(QVariant, health_count)) ||
      health_count.toInt() != 4 ||
      !health_signature.toString().contains(QStringLiteral("GPU:")) ||
      !health_signature.toString().contains(QStringLiteral("Model:")) ||
      !health_signature.toString().contains(QStringLiteral("Catalog:")) ||
      !health_signature.toString().contains(QStringLiteral("Export:")) ||
      edit_workspace->findChild<QObject*>(QStringLiteral("healthStatusStrip")) == nullptr) {
    return fail("workspace should expose in-app health status for GPU, model, catalog, and export");
  }
  QVariant histogram_available;
  QVariant waveform_available;
  QVariant parade_available;
  QVariant vectorscope_available;
  QVariant scope_channel_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, histogram_available),
          Q_ARG(QVariant, QStringLiteral("Histogram"))) ||
      !histogram_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, waveform_available),
          Q_ARG(QVariant, QStringLiteral("Waveform"))) ||
      !waveform_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, parade_available),
          Q_ARG(QVariant, QStringLiteral("Parade"))) ||
      !parade_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, vectorscope_available),
          Q_ARG(QVariant, QStringLiteral("Vectorscope"))) ||
      !vectorscope_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeChannelSignature",
          Q_RETURN_ARG(QVariant, scope_channel_signature)) ||
      !scope_channel_signature.toString().contains(QStringLiteral("rgb:true")) ||
      !scope_channel_signature.toString().contains(QStringLiteral("luma:true"))) {
    return fail("workspace should expose RGB and luma histogram scope channels");
  }
  QVariant scope_layout_result;
  QVariant scope_sampling_result;
  QVariant updated_scope_signature;
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("scopes"));
  edit_workspace->setProperty("largeScopesMode", false);
  QCoreApplication::processEvents();
  auto* inspector_scope_grid = find_named_object(edit_workspace, QStringLiteral("inspectorScopeGrid"));
  auto* scope_async_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("scopeAsyncStatusLabel"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopeLayoutPreset",
          Q_RETURN_ARG(QVariant, scope_layout_result),
          Q_ARG(QVariant, QStringLiteral("Quad"))) ||
      !scope_layout_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setScopeSamplingMode",
          Q_RETURN_ARG(QVariant, scope_sampling_result),
          Q_ARG(QVariant, QStringLiteral("Precise"))) ||
      !scope_sampling_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeChannelSignature",
          Q_RETURN_ARG(QVariant, updated_scope_signature)) ||
      !updated_scope_signature.toString().contains(QStringLiteral("layout:Quad")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("sampling:Precise")) ||
      inspector_scope_grid == nullptr ||
      inspector_scope_grid->property("effectiveColumns").toInt() != 1 ||
      scope_async_status_label == nullptr ||
      !scope_async_status_label->property("text").toString().contains(QStringLiteral("Scopes"))) {
    return fail("workspace should expose scope layout presets and sampling controls");
  }
  edit_workspace->setProperty("largeScopesMode", true);
  QCoreApplication::processEvents();
  auto* large_scope_grid = find_named_object(edit_workspace, QStringLiteral("largeScopeGrid"));
  if (large_scope_grid == nullptr || large_scope_grid->property("effectiveColumns").toInt() != 2 ||
      edit_workspace->property("editContextPanelActive").toBool()) {
    return fail("large scope mode should use a two-column scope grid and release context-panel space");
  }

  root->setProperty("currentWorkspace", root->property("libraryWorkspaceIndex"));
  QCoreApplication::processEvents();

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("survey-1"), QStringLiteral("IMG_0101.jpg")),
       make_asset(QStringLiteral("survey-2"), QStringLiteral("IMG_0102.jpg")),
       make_asset(QStringLiteral("survey-3"), QStringLiteral("IMG_0103.jpg")),
       make_asset(QStringLiteral("survey-4"), QStringLiteral("IMG_0104.jpg"))});
  catalog_assets.selectIndex(1);
  QCoreApplication::processEvents();
  auto* library_workspace = root->findChild<QObject*>(QStringLiteral("libraryStudioWorkspace"));
  if (library_workspace == nullptr) {
    return fail("library workspace should expose a testable StudioWorkspace object");
  }
  auto* library_sources_panel = library_workspace->findChild<QObject*>(QStringLiteral("librarySourcesPanel"));
  auto* library_inspector_panel = library_workspace->findChild<QObject*>(QStringLiteral("libraryInspectorPanel"));
  if (library_sources_panel == nullptr || library_inspector_panel == nullptr ||
      library_sources_panel->property("title").toString() != QStringLiteral("Sources") ||
      library_inspector_panel->property("title").toString() != QStringLiteral("Inspector")) {
    return fail("library side panels should use readable title-case labels");
  }
  library_workspace->setProperty("catalogViewMode", QStringLiteral("survey"));
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  if (catalog_assets.surveyAssetCount(catalog_assets.selectedIndex(), 6) < 3 ||
      library_workspace->property("catalogViewMode").toString() != QStringLiteral("survey")) {
    return fail("library workspace should expose a populated survey view model");
  }

  if (!QMetaObject::invokeMethod(root, "beginImportWorkspace")) {
    return fail("menu routing should expose beginImportWorkspace");
  }
  QCoreApplication::processEvents();
  if (root->property("currentWorkspace").toInt() != 0 ||
      root->property("libraryWorkspaceMode").toString() != QStringLiteral("import")) {
    return fail("beginImportWorkspace should route to Library import mode");
  }

  if (!QMetaObject::invokeMethod(root, "createTimelineFromMenu")) {
    return fail("menu routing should expose createTimelineFromMenu");
  }
  QCoreApplication::processEvents();
  if (root->property("currentWorkspace").toInt() != root->property("timelineWorkspaceIndex").toInt() ||
      timeline_document.timelineName().trimmed().isEmpty()) {
    return fail("createTimelineFromMenu should route to Timeline and initialize a document");
  }

  const auto library_workspace_index = root->property("libraryWorkspaceIndex").toInt();
  const auto edit_workspace_index = root->property("editWorkspaceIndex").toInt();
  const auto carousel_workspace_index = root->property("carouselWorkspaceIndex").toInt();
  const auto timeline_workspace_index = root->property("timelineWorkspaceIndex").toInt();
  const auto export_workspace_index = root->property("exportWorkspaceIndex").toInt();
  const QList<int> expected_workspace_order = {
      library_workspace_index,
      edit_workspace_index,
      carousel_workspace_index,
      timeline_workspace_index,
      export_workspace_index,
  };
  for (auto number = 1; number <= expected_workspace_order.size(); ++number) {
    QVariant returned_value;
    const auto invoked = QMetaObject::invokeMethod(
        root,
        "switchWorkspaceByNumber",
        Q_RETURN_ARG(QVariant, returned_value),
        Q_ARG(QVariant, number));
    QCoreApplication::processEvents();
    if (!invoked || !returned_value.toBool() ||
        root->property("currentWorkspace").toInt() != expected_workspace_order.at(number - 1)) {
      return fail("global workspace number shortcuts should route across every workspace");
    }
  }
  auto* export_workspace = root->findChild<QObject*>(QStringLiteral("exportStudioWorkspace"));
  auto* export_presets_panel = export_workspace != nullptr
      ? export_workspace->findChild<QObject*>(QStringLiteral("exportPresetsPanel"))
      : nullptr;
  auto* export_settings_panel = export_workspace != nullptr
      ? export_workspace->findChild<QObject*>(QStringLiteral("exportSettingsPanel"))
      : nullptr;
  if (export_workspace == nullptr || export_presets_panel == nullptr || export_settings_panel == nullptr ||
      export_presets_panel->property("title").toString() != QStringLiteral("Export Presets") ||
      export_settings_panel->property("title").toString() != QStringLiteral("Output Settings")) {
    return fail("export panels should use readable title-case labels");
  }
  QVariant invalid_workspace_shortcut;
  const auto invalid_invoked = QMetaObject::invokeMethod(
      root,
      "switchWorkspaceByNumber",
      Q_RETURN_ARG(QVariant, invalid_workspace_shortcut),
      Q_ARG(QVariant, expected_workspace_order.size() + 1));
  if (!invalid_invoked || invalid_workspace_shortcut.toBool()) {
    return fail("global workspace shortcut routing should reject unknown workspace numbers");
  }
  root->setProperty("currentWorkspace", edit_workspace_index);
  QCoreApplication::processEvents();
  if (root->property("activityIndicatorBottomMargin").toInt() < 96) {
    return fail("edit workspace activity indicator should clear the filmstrip and footer controls");
  }
  root->setProperty("currentWorkspace", library_workspace_index);
  QCoreApplication::processEvents();
  if (root->property("activityIndicatorBottomMargin").toInt() != 12) {
    return fail("non-edit activity indicator should stay close to the workspace edge");
  }
  QVariant export_cancel_supported;
  QVariant preview_cancel_supported;
  QVariant activity_signature;
  if (!QMetaObject::invokeMethod(
          root,
          "supportsLongOperationCancel",
          Q_RETURN_ARG(QVariant, export_cancel_supported),
          Q_ARG(QVariant, QStringLiteral("asset_export"))) ||
      !export_cancel_supported.toBool() ||
      !QMetaObject::invokeMethod(
          root,
          "supportsLongOperationCancel",
          Q_RETURN_ARG(QVariant, preview_cancel_supported),
          Q_ARG(QVariant, QStringLiteral("preview_render"))) ||
      preview_cancel_supported.toBool() ||
      !QMetaObject::invokeMethod(
          root,
          "primaryActivityControlSignature",
          Q_RETURN_ARG(QVariant, activity_signature)) ||
      !activity_signature.toString().contains(QStringLiteral("|")) ||
      root->findChild<QObject*>(QStringLiteral("floatingActivityIndicator")) == nullptr ||
      root->findChild<QObject*>(QStringLiteral("activityCancelButton")) == nullptr) {
    return fail("app should expose visible progress and cancel controls for long operations");
  }
  auto* command_palette = root->findChild<QObject*>(QStringLiteral("commandPaletteDialog"));
  if (command_palette == nullptr || command_palette->property("commandCount").toInt() < 60) {
    return fail("command palette should expose broad global and workspace command coverage");
  }
  auto* keyboard_shortcuts = root->findChild<QObject*>(QStringLiteral("keyboardShortcutsDialog"));
  QVariant edited_shortcut;
  QVariant reset_shortcut;
  QVariant library_shortcut_sequence;
  QVariant would_conflict;
  QVariant rejected_conflict;
  if (keyboard_shortcuts == nullptr ||
      keyboard_shortcuts->property("editableShortcutCount").toInt() < 5 ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "setShortcutSequence",
          Q_RETURN_ARG(QVariant, edited_shortcut),
          Q_ARG(QVariant, QStringLiteral("command-palette")),
          Q_ARG(QVariant, QStringLiteral("Ctrl+Alt+K"))) ||
      !edited_shortcut.toBool() ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "resetShortcutSequence",
           Q_RETURN_ARG(QVariant, reset_shortcut),
           Q_ARG(QVariant, QStringLiteral("command-palette"))) ||
      !reset_shortcut.toBool()) {
    return fail("keyboard shortcuts dialog should expose editable shortcut mappings");
  }
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutSequence",
          Q_RETURN_ARG(QVariant, library_shortcut_sequence),
          Q_ARG(QVariant, QStringLiteral("workspace-library"))) ||
      library_shortcut_sequence.toString().trimmed().isEmpty() ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutWouldConflict",
          Q_RETURN_ARG(QVariant, would_conflict),
          Q_ARG(QVariant, QStringLiteral("workspace-edit")),
          Q_ARG(QVariant, library_shortcut_sequence.toString())) ||
      !would_conflict.toBool() ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "setShortcutSequence",
          Q_RETURN_ARG(QVariant, rejected_conflict),
          Q_ARG(QVariant, QStringLiteral("workspace-edit")),
          Q_ARG(QVariant, library_shortcut_sequence.toString())) ||
      rejected_conflict.toBool() ||
      !keyboard_shortcuts->property("shortcutEditorStatus")
           .toString()
           .contains(QStringLiteral("conflicts"))) {
    return fail("keyboard shortcuts dialog should detect and reject conflicting mappings");
  }
  if (keyboard_shortcuts->property("shortcutPresetCount").toInt() < 4) {
    return fail("keyboard shortcuts dialog should expose Lightroom, Photoshop, Capture One, and app-native presets");
  }
  const QString shortcut_presets[] = {
      QStringLiteral("lightroom"),
      QStringLiteral("photoshop"),
      QStringLiteral("capture-one"),
      QStringLiteral("app-native"),
  };
  for (const auto& preset_id : shortcut_presets) {
    QVariant applied_preset;
    QVariant conflict_count;
    if (!QMetaObject::invokeMethod(
            keyboard_shortcuts,
            "applyShortcutPreset",
            Q_RETURN_ARG(QVariant, applied_preset),
            Q_ARG(QVariant, preset_id)) ||
        !applied_preset.toBool() ||
        keyboard_shortcuts->property("activeShortcutPreset").toString() != preset_id ||
        !QMetaObject::invokeMethod(
            keyboard_shortcuts,
            "shortcutConflictCount",
            Q_RETURN_ARG(QVariant, conflict_count)) ||
        conflict_count.toInt() != 0) {
      return fail("keyboard shortcuts dialog should apply named shortcut presets without conflicts");
    }
  }
  keyboard_shortcuts->setProperty("shortcutSearchText", QStringLiteral("timeline"));
  QVariant timeline_shortcut_results;
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutSearchResultCount",
          Q_RETURN_ARG(QVariant, timeline_shortcut_results)) ||
      timeline_shortcut_results.toInt() < 4) {
    return fail("keyboard shortcuts dialog should search shortcut descriptions and mappings");
  }
  keyboard_shortcuts->setProperty("shortcutSearchText", QStringLiteral("zzzz-no-shortcut"));
  QVariant empty_shortcut_results;
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutSearchResultCount",
          Q_RETURN_ARG(QVariant, empty_shortcut_results)) ||
      empty_shortcut_results.toInt() != 0) {
    return fail("keyboard shortcuts dialog should report empty shortcut search results");
  }
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  qunsetenv("PHOTO_EDITOR_CACHE_DIR");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
