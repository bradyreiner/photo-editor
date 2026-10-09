#pragma once

#include <QClipboard>
#include <QMimeData>
#include <QQmlComponent>
#include <QPointer>
#include "photo_recipe_clipboard.h"
#include "native_window_test_input.h"

// Included after the workflow driver's fixture, capture and event-loop helpers.
int run_photo_recipe_clipboard_proof(
    QQuickWindow* window, photo_editor::desktop::CatalogAssetModel& catalog,
    photo_editor::desktop::EditorPreviewController& editor,
    QQmlApplicationEngine& engine, const QString& source_path, const QString& output_dir) {
  using namespace photo_editor::desktop;
  auto* clipboard = QGuiApplication::clipboard();
  auto* saved_clipboard = new QMimeData;
  if (const auto* previous = clipboard->mimeData()) {
    for (const auto& format : previous->formats()) saved_clipboard->setData(format, previous->data(format));
  }
  const auto restore_clipboard = qScopeGuard([&] { clipboard->setMimeData(saved_clipboard); });
  QJsonArray passed;
  auto check = [&](bool ok, const QString& label) {
    if (ok) { passed.append(label); std::cout << "PASS: " << label.toStdString() << std::endl; }
    else std::cerr << "Recipe clipboard proof: " << label.toStdString() << '\n';
    return ok;
  };
  auto patch_for = [](const EditRecipeState& state) {
    return serialize_adjustment_patch_json(state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  };
  auto payload = [&](const QString& id) { return catalog.assetById(id).value("adjustmentPatchJson").toString(); };
  auto report_slow_ui_call = [](const char* call, qint64 elapsed_ms) {
    if (elapsed_ms < 50) return;
    std::cerr << "UI_TIMING utc="
              << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()
              << " call=" << call << " elapsed_ms=" << elapsed_ms << std::endl;
  };
  auto state_for = [&](const QString& id) {
    const auto parsed = parse_adjustment_payload_or_legacy_recipe(payload(id));
    return parsed.ok() ? *parsed.state : EditRecipeState{};
  };
  auto select_single = [&](int index) {
    QElapsedTimer timer;
    timer.start();
    catalog.clearSelection();
    catalog.selectIndex(index);
    report_slow_ui_call("select_single", timer.elapsed());
  };
  auto set_workspace = [&](const char* index_property) {
    QElapsedTimer timer;
    timer.start();
    window->setProperty("currentWorkspace", window->property(index_property));
    report_slow_ui_call("set_current_workspace", timer.elapsed());
  };
  auto invoke = [&](const char* method) { return QMetaObject::invokeMethod(window, method); };
  auto focus_window = [&] {
    window->requestActivate();
    auto* library_tab = find_named_item(window, QStringLiteral("workspaceTabLibrary"));
    if (library_tab != nullptr) {
      library_tab->forceActiveFocus(Qt::TabFocusReason);
    } else {
      window->contentItem()->forceActiveFocus();
    }
    process_layout_events(4);
  };
  auto key = [&](int code, int modifiers) {
    QElapsedTimer input_timer;
    input_timer.start();
    key_native_test_window(window, code, modifiers);
    if (input_timer.elapsed() >= 50) {
      std::cerr << "UI_TIMING utc="
                << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()
                << " call=key_native_test_window elapsed_ms=" << input_timer.elapsed()
                << std::endl;
    }
    process_layout_events(4);
  };
  auto click = [&](const char* name) {
    auto* item = find_named_item(window, QString::fromLatin1(name));
    if (!item || !wait_for_condition([&] { return item->isVisible() && item->isEnabled() && item->width() > 0; })) {
      std::cerr << "Missing/disabled click target: " << name << " found=" << (item != nullptr) << '\n';
      QDir().mkpath(output_dir);
      window->grabWindow().save(QDir(output_dir).filePath("failed-click.png"));
      return false;
    }
    QElapsedTimer input_timer;
    input_timer.start();
    click_native_test_window(window, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    if (input_timer.elapsed() >= 50) {
      std::cerr << "UI_TIMING utc="
                << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()
                << " call=click_native_test_window target=" << name
                << " elapsed_ms=" << input_timer.elapsed() << std::endl;
    }
    process_layout_events(4);
    return true;
  };
  auto* dialog = window->findChild<QObject*>("copyPhotoEditsDialog");
  if (!check(dialog != nullptr, "Copy chooser is part of Main.qml")) return EXIT_FAILURE;
  EditRecipeState source;
  source.exposure = 1.1;
  source.saturation = 28;
  source.temperature = 12;
  source.sharpening = 43;
  source.raw_black_level = 0.12;
  source.linear_mask_enabled = true;
  source.linear_mask_exposure = 0.65;
  source.layers = {QVariantMap{{"id", "clipboard-layer"}, {"kind", "adjustment"},
      {"name", "Local polish"}, {"opacity", 0.7}, {"blendMode", "normal"}}};
  EditRecipeState target;
  target.exposure = -0.25;
  target.saturation = -10;
  target.input_color_profile = QStringLiteral("sRGB");
  target.sharpening = 22;
  target.crop_rect_width = 0.7;
  target.crop_rect_height = 0.8;
  auto source_item = make_asset("copy-source", source_path);
  source_item.adjustment_patch_json = patch_for(source);
  auto first = make_asset("paste-first", source_path);
  first.adjustment_patch_json = patch_for(target);
  auto second = make_asset("paste-second", source_path);
  target.exposure = -0.7;
  second.adjustment_patch_json = patch_for(target);
  auto neutral = make_asset("copy-neutral", source_path);
  auto video = make_asset("skip-video", source_path);
  video.media_type = "video";
  catalog.setInteractiveRenderingActive(true);
  QVector<CatalogAssetItem> fixtures{source_item, first, second, neutral, video};
  for (int index = 5; index < 200; ++index)
    fixtures.append(make_asset(QStringLiteral("range-photo-%1").arg(index), source_path));
  QElapsedTimer fixture_timer;
  fixture_timer.start();
  catalog.replaceAssetsForTesting(fixtures);
  report_slow_ui_call("replace_clipboard_proof_fixtures", fixture_timer.elapsed());
  select_single(0);
  process_layout_events(8);
  for (const auto& item : {source_item, first, second, neutral}) {
    catalog.applyThumbnailResultForTesting(item.asset_id, item.file_path, payload(item.asset_id),
        QUrl::fromLocalFile(source_path).toString());
  }
  process_layout_events(8);
  auto click_asset = [&](int index, int modifiers) {
    std::function<QQuickItem*(QQuickItem*)> find_tile = [&](QQuickItem* item) -> QQuickItem* {
      if (item->objectName() == "libraryGalleryTile" && item->property("assetIndex").toInt() == index)
        return item;
      for (auto* child : item->childItems()) if (auto* found = find_tile(child)) return found;
      return nullptr;
    };
    QQuickItem* tile = nullptr;
    if (!wait_for_condition([&] { tile = find_tile(window->contentItem()); return tile && tile->isVisible(); }))
      return false;
    // A virtualized row may exist before its positioner has been polished.
    // Capture a frame before mapping the native pointer position.
    QElapsedTimer frame_timer;
    frame_timer.start();
    static_cast<void>(window->grabWindow());
    if (frame_timer.elapsed() >= 50) {
      std::cerr << "UI_TIMING utc="
                << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()
                << " call=grabWindow_before_gallery_click elapsed_ms=" << frame_timer.elapsed()
                << std::endl;
    }
    auto* viewport = find_named_item(window, "studioLibraryGalleryGrid");
    if (!viewport) return false;
    const auto visible_rect = QRectF(tile->mapToScene(QPointF()), tile->size()).intersected(
        QRectF(viewport->mapToScene(QPointF()), viewport->size()));
    if (visible_rect.width() < 4 || visible_rect.height() < 4) return false;
    // Click the visible photo near its upper-left corner, clear of the floating
    // agent launcher that can cover the center of a bottom-right tile.
    QElapsedTimer input_timer;
    input_timer.start();
    modified_click_native_test_window(window,
        (visible_rect.topLeft() + QPointF(std::min(24.0, visible_rect.width() / 2),
            std::min(24.0, visible_rect.height() / 2))).toPoint(), modifiers);
    if (input_timer.elapsed() >= 50) {
      std::cerr << "UI_TIMING utc="
                << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()
                << " call=modified_gallery_click index=" << index
                << " elapsed_ms=" << input_timer.elapsed() << std::endl;
    }
    process_layout_events(4);
    return true;
  };
  int selection_notifications = 0;
  const auto selection_connection = QObject::connect(&catalog, &CatalogAssetModel::selectionChanged,
      [&] { ++selection_notifications; });
  const auto disconnect_selection = qScopeGuard([&] { QObject::disconnect(selection_connection); });
  if (!click_asset(1, Qt::NoModifier)) return EXIT_FAILURE;
  const auto before_range = selection_notifications;
  if (!click_asset(3, Qt::ShiftModifier) || !check(catalog.selectedAssetIds() ==
      QStringList{"paste-first", "paste-second", "copy-neutral"} && selection_notifications == before_range + 1,
      "Shift-click selects an inclusive range with one selection notification")) return EXIT_FAILURE;
  if (!click_asset(2, Qt::ShiftModifier) || !check(catalog.selectedAssetIds() ==
      QStringList{"paste-first", "paste-second"}, "Repeated Shift-click shrinks from the original anchor")) return EXIT_FAILURE;
  if (!click_asset(3, Qt::NoModifier) || !click_asset(1, Qt::ShiftModifier) || !check(catalog.selectedAssetIds() ==
      QStringList{"paste-first", "paste-second", "copy-neutral"}, "Shift-click selects a backward range")) return EXIT_FAILURE;
  if (!click_asset(4, Qt::ControlModifier) || !click_asset(2, Qt::MetaModifier) || !check(catalog.selectedAssetIds() ==
      QStringList{"paste-first", "copy-neutral", "skip-video"}, "Ctrl/Cmd-click adds and removes individual photos")) return EXIT_FAILURE;
  if (!click_asset(0, Qt::ControlModifier | Qt::ShiftModifier) || !check(catalog.selectedAssetCount() == 5,
      "Ctrl+Shift-click adds a range to an existing selection")) return EXIT_FAILURE;
  if (!click_asset(0, Qt::NoModifier) || !click_asset(2, Qt::MetaModifier) || !check(catalog.selectedAssetIds() ==
      QStringList{"copy-source", "paste-second"}, "Normal click resets selection and Cmd-click keeps the first photo")) return EXIT_FAILURE;
  if (!click_asset(0, Qt::NoModifier)) return EXIT_FAILURE;
  auto* gallery = find_named_item(window, "studioLibraryGalleryGrid");
  if (!gallery) return EXIT_FAILURE;
  gallery->setProperty("contentY", gallery->property("contentHeight").toDouble() - gallery->height());
  process_layout_events(8);
  if (!click_asset(199, Qt::ShiftModifier) || !check(catalog.selectedAssetCount() == 200,
      "Shift-click includes off-screen photos in a virtualized Library")) {
    std::cerr << "Range selected=" << catalog.selectedAssetCount() << " active=" << catalog.selectedIndex() << '\n';
    QDir().mkpath(output_dir);
    window->grabWindow().save(QDir(output_dir).filePath("failed-range.png"));
    return EXIT_FAILURE;
  }
  gallery->setProperty("contentY", 0.0);
  process_layout_events(8);
  select_single(0);
  clipboard->setText("keep this clipboard until Copy is confirmed");
  if (!check(!editor.hasCopiedPhotoRecipe() &&
      !editor.copyPhotoRecipe(source_item.adjustment_patch_json, {}) &&
      !editor.copyPhotoRecipe(source_item.adjustment_patch_json, {"unknown"}) &&
      !editor.copyPhotoRecipe("not a recipe", {"color"}), "Invalid/empty group copies preserve clipboard")) return EXIT_FAILURE;
  focus_window();
  QPointer<QQuickItem> cancel_focus_target(window->activeFocusItem());
  key(Qt::Key_C, Qt::ControlModifier);
  if (!check(wait_for_condition([&] { return dialog->property("visible").toBool(); }), "Ctrl+C opens chooser")) return EXIT_FAILURE;
  escape_native_test_window(window);
  process_layout_events(4);
  const auto cancel_restored = wait_for_condition([&] {
    const auto* active_focus = window->activeFocusItem();
    const auto expected_focus = cancel_focus_target.isNull()
        ? window->contentItem()
        : cancel_focus_target.data();
    return active_focus != nullptr && !dialog->property("visible").toBool() &&
        active_focus == expected_focus;
  }, 1000);
  const auto clipboard_preserved = clipboard->text().startsWith("keep this");
  if (!cancel_restored || !clipboard_preserved) {
    const QPointer<QQuickItem> active_focus(window->activeFocusItem());
    std::cerr << "Copy Edits cancel diagnostic: visible=" << dialog->property("visible").toBool()
              << " clipboardPreserved=" << clipboard_preserved
              << " expectedFocus=" << (cancel_focus_target ? cancel_focus_target->objectName().toStdString() : "<destroyed>")
              << " activeFocus=" << (active_focus ? active_focus->objectName().toStdString() : "<null>")
              << " fallbackFocus=" << (window->contentItem() ? window->contentItem()->objectName().toStdString() : "<null>")
              << '\n';
    return fail("Escape should cancel Copy Edits, preserve the clipboard, and restore focus within one second");
  }
  if (!check(true, "Escape cancels and restores focus without changing the clipboard")) return EXIT_FAILURE;
  focus_window();
  key(Qt::Key_C, Qt::MetaModifier);
  if (!check(wait_for_condition([&] { return dialog->property("visible").toBool(); }), "Cmd+C opens chooser")) return EXIT_FAILURE;
  auto* first_group = find_named_item(window, "copyPhotoEditsGroup_tone");
  const auto first_group_focused = wait_for_condition([&] {
    first_group = find_named_item(window, "copyPhotoEditsGroup_tone");
    return first_group != nullptr && window->activeFocusItem() == first_group;
  }, 1000);
  if (!first_group_focused) {
    const QPointer<QQuickItem> active_focus(window->activeFocusItem());
    const QPointer<QQuickItem> group_focus(first_group);
    std::cerr << "Copy Edits open-focus diagnostic: visible=" << dialog->property("visible").toBool()
              << " groupFound=" << !group_focus.isNull()
              << " groupVisible=" << (group_focus && group_focus->isVisible())
              << " groupActive=" << (group_focus && group_focus->hasActiveFocus())
              << " groupFocus=" << (group_focus ? group_focus->hasFocus() : false)
              << " active=" << (active_focus ? active_focus->objectName().toStdString() : "<null>")
              << " group=" << (group_focus ? group_focus->objectName().toStdString() : "<null>")
              << '\n';
    save_capture_if_requested(window, output_dir, "copy-focus-failed", nullptr);
    return fail("Copy Edits opens with the first named group focused");
  }
  if (!click("copyPhotoEditsClear")) return EXIT_FAILURE;
  if (!check(!find_named_item(window, "copyPhotoEditsConfirm")->isEnabled(), "Copy is disabled when no groups are selected")) return EXIT_FAILURE;
  if (!click("copyPhotoEditsGroup_color")) return EXIT_FAILURE;
  QJsonArray captures;
  process_layout_events(8);
  save_capture_if_requested(window, output_dir, "copy-color-chooser", &captures);
  if (!click("copyPhotoEditsConfirm")) return EXIT_FAILURE;
  const auto copied = parse_adjustment_patch_json(editor.copiedPhotoRecipePatchJson());
  if (!check(copied.ok() && copied.groups == QStringList{"color"} && copied.state->saturation == 28,
      "Chooser copies exactly Color")) return EXIT_FAILURE;
  if (!click_asset(1, Qt::NoModifier) || !click_asset(2, Qt::ShiftModifier) ||
      !click_asset(4, Qt::ControlModifier)) return EXIT_FAILURE;
  focus_window();
  key(Qt::Key_V, Qt::ControlModifier);
  if (!check(state_for("paste-first").saturation == 28 && state_for("paste-second").saturation == 28 &&
      state_for("paste-first").exposure == -0.25 && state_for("paste-second").exposure == -0.7 &&
      state_for("paste-first").sharpening == 22 && state_for("paste-first").crop_rect_width == 0.7 &&
      payload("skip-video").isEmpty(), "Batch paste preserves Light, Detail, Crop and skips videos")) return EXIT_FAILURE;
  save_capture_if_requested(window, output_dir, "batch-pasted", &captures);
  if (!click("undoPastedPhotoEdits") || !check(payload("paste-first") == first.adjustment_patch_json &&
      payload("paste-second") == second.adjustment_patch_json, "One-click undo restores both complete recipes")) return EXIT_FAILURE;
  catalog.redoBatchRecipe();
  if (!check(state_for("paste-first").saturation == 28 && state_for("paste-second").saturation == 28,
      "Batch redo restores both pasted recipes")) return EXIT_FAILURE;
  catalog.undoBatchRecipe();
  select_single(1);
  set_workspace("editWorkspaceIndex");
  if (!check(wait_for_condition([&] { return editor.assetId() == "paste-first"; }, 15000), "Single target opens in Edit")) return EXIT_FAILURE;
  QElapsedTimer edit_timer;
  edit_timer.start();
  editor.setExposure(0.85);
  report_slow_ui_call("set_exposure", edit_timer.elapsed());
  focus_window();
  key(Qt::Key_V, Qt::MetaModifier);
  if (!check(editor.saturation() == 28 && editor.exposure() == 0.85 && state_for("paste-first").exposure == 0.85,
      "Cmd+V merges onto unsaved active-photo edits")) return EXIT_FAILURE;
  key(Qt::Key_Z, Qt::MetaModifier);
  if (!check(editor.saturation() == -10 && editor.exposure() == 0.85, "Single-photo paste is one editor undo step")) return EXIT_FAILURE;
  select_single(2);
  editor.setSaturation(-15);
  set_workspace("libraryWorkspaceIndex");
  select_single(1);
  catalog.toggleSelectionIndex(1);
  catalog.toggleSelectionIndex(2);
  focus_window();
  key(Qt::Key_V, Qt::ControlModifier);
  select_single(2);
  set_workspace("editWorkspaceIndex");
  if (!check(wait_for_condition([&] { return editor.assetId() == "paste-second" && editor.saturation() == 28; }, 15000),
      "Reopening a cached photo keeps the pasted recipe")) return EXIT_FAILURE;
  invoke("undoPastedPhotoEdits");
  if (!check(editor.saturation() == -15 && state_for("paste-second").saturation == -15,
      "Batch undo refreshes the active cached recipe")) return EXIT_FAILURE;
  catalog.redoBatchRecipe();
  if (!check(editor.saturation() == 28 && state_for("paste-second").saturation == 28,
      "Batch redo refreshes the active cached recipe")) return EXIT_FAILURE;
  set_workspace("libraryWorkspaceIndex");
  const auto before_masks = state_for("paste-first");
  if (!editor.copyPhotoRecipe(source_item.adjustment_patch_json, {"masks"})) return EXIT_FAILURE;
  const auto masks_copy = parse_adjustment_patch_json(editor.copiedPhotoRecipePatchJson());
  if (!check(masks_copy.ok() && masks_copy.groups.contains("layers"), "Masks include their adjustment layers")) return EXIT_FAILURE;
  select_single(1);
  focus_window();
  key(Qt::Key_V, Qt::ControlModifier);
  const auto masked = state_for("paste-first");
  if (!check(masked.layers.size() == 1 && !masked.mask_stack.isEmpty() &&
      masked.exposure == before_masks.exposure && masked.saturation == before_masks.saturation &&
      masked.sharpening == before_masks.sharpening, "Mask paste preserves all other target edit groups")) return EXIT_FAILURE;
  select_single(3);
  focus_window();
  key(Qt::Key_C, Qt::ControlModifier);
  if (!click("copyPhotoEditsClear") || !click("copyPhotoEditsGroup_color") || !click("copyPhotoEditsConfirm")) return EXIT_FAILURE;
  select_single(1);
  focus_window();
  key(Qt::Key_V, Qt::ControlModifier);
  if (!check(state_for("paste-first").saturation == 0 && state_for("paste-first").layers == masked.layers &&
      payload("copy-source") == source_item.adjustment_patch_json, "Unedited source resets only copied groups and source stays intact")) return EXIT_FAILURE;
  const auto history_index = catalog.batchRecipeHistoryCurrentIndex();
  invoke("pastePhotoEdits");
  if (!check(catalog.batchRecipeHistoryCurrentIndex() == history_index, "No-op paste adds no history")) return EXIT_FAILURE;
  if (!editor.copyPhotoRecipe(source_item.adjustment_patch_json, {"raw"}) ||
      catalog.pasteAdjustmentPatchJsonToAssetIds({"paste-first", "paste-first"}, editor.copiedPhotoRecipePatchJson()) != 1 ||
      !check(state_for("paste-first").raw_black_level == 0.12 &&
          state_for("paste-first").input_color_profile == "sRGB", "RAW paste keeps target input profile and deduplicates targets")) return EXIT_FAILURE;
  // Copy in Edit must capture a live, not-yet-saved source revision.
  select_single(0);
  set_workspace("editWorkspaceIndex");
  if (!wait_for_condition([&] { return editor.assetId() == "copy-source"; }, 15000)) return EXIT_FAILURE;
  editor.setSaturation(37);
  focus_window();
  key(Qt::Key_C, Qt::ControlModifier);
  if (!click("copyPhotoEditsConfirm")) return EXIT_FAILURE;
  const auto live_copy = parse_adjustment_patch_json(editor.copiedPhotoRecipePatchJson());
  if (!check(live_copy.ok() && live_copy.state->saturation == 37, "Copy includes unsaved source edits")) return EXIT_FAILURE;
  set_workspace("libraryWorkspaceIndex");
  QQmlComponent text_component(&engine);
  text_component.setData("import QtQuick\nTextInput { width: 280; height: 40; text: 'Normal text copy'; selectByMouse: true }", QUrl());
  std::unique_ptr<QObject> text_object(text_component.create());
  auto* input = qobject_cast<QQuickItem*>(text_object.get());
  if (!input) return fail("Text clipboard proof could not create TextInput");
  input->setParentItem(window->contentItem());
  input->forceActiveFocus();
  QMetaObject::invokeMethod(input, "selectAll");
  key(Qt::Key_C, Qt::ControlModifier);
  if (!check(clipboard->text() == "Normal text copy" && !dialog->property("visible").toBool(), "Text-field copy keeps normal behavior")) return EXIT_FAILURE;
  input->setProperty("text", "");
  key(Qt::Key_V, Qt::ControlModifier);
  if (!check(input->property("text").toString() == "Normal text copy" && !editor.hasCopiedPhotoRecipe(), "Text-field paste and clipboard availability update normally")) return EXIT_FAILURE;
  input->setParentItem(nullptr);
  QDir().mkpath(output_dir);
  QFile report(QDir(output_dir).filePath("recipe-clipboard-proof.json"));
  if (!report.open(QIODevice::WriteOnly)) return EXIT_FAILURE;
  report.write(QJsonDocument(QJsonObject{{"passed", passed}, {"captures", captures},
      {"input", "Native Qt window keyboard and pointer events"}}).toJson(QJsonDocument::Indented));
  std::cout << "Photo recipe clipboard proof passed: " << passed.size() << " checks.\n";
  return EXIT_SUCCESS;
}
