void verify_board_photo_recipe_framing(const QTemporaryDir& runtime) {
  QImage base(120, 80, QImage::Format_RGB32); base.fill(Qt::black);
  QImage photo(80, 240, QImage::Format_RGB32);
  for (int y = 0; y < photo.height(); ++y) for (int x = 0; x < photo.width(); ++x)
    photo.setPixelColor(x, y, y < 80 ? QColor(64, 16, 8) : y < 160 ? QColor(8, 64, 16) : QColor(16, 8, 64));
  const auto base_path = runtime.filePath("framing-canvas.png"), photo_path = runtime.filePath("framing-original.png");
  require(base.save(base_path) && photo.save(photo_path), "Save photo recipe and framing fixtures");
  EditRecipeState develop; develop.exposure = 1.0;
  const auto source_recipe = serialize_edit_recipe_json(develop);
  EditorPreviewController editor;
  editor.loadAsset("framing-board", base_path, {}, {}, {}, {}, {}, true); settle(editor);
  require(editor.addSourceLayer(photo_path, "Edited top photo", source_recipe, "edited-photo") &&
          editor.addSourceLayer(photo_path, "Previously placed photo") &&
          editor.setSourceLayerRecipe(0, source_recipe, "edited-photo"),
          "New and previously placed photos retain their full saved recipes and source identities"); settle(editor);
  auto legacy = parse_adjustment_payload_or_legacy_recipe(editor.currentAssetBoundAdjustmentPatchJson());
  photo_editor::pipeline::LayerStyleStack old_borders;
  photo_editor::pipeline::LayerStyleOperation old_border;
  old_border.id = "board-photo-border"; old_border.type_id = "stroke";
  old_border.parameters = photo_editor::pipeline::LayerStyleStroke{.width = 4};
  old_borders.operations.push_back(old_border);
  const auto old_style = photo_editor::pipeline::serialize_layer_style_stack(old_borders);
  require(old_style.has_value(), "Prepare an existing board's individual border styles");
  for (auto& value : legacy.state->layers) {
    auto layer = value.toMap(); layer.insert("layerStyleStack", QString::fromStdString(*old_style)); value = layer;
  }
  editor.loadAsset("framing-board", base_path, {}, {}, {}, {}, serialize_edit_recipe_json(*legacy.state), true); settle(editor);
  const auto before_layout = editor.currentAssetBoundAdjustmentPatchJson();
  const auto before_layout_history = editor.editHistoryCurrentIndex();
  require(editor.applyBoardPhotoLayout({0, 1}, "stack", {{"margin", 4}, {"spacing", 4}, {"border", 2}, {"displayReferredCanvas", true}}),
          "Arrange edited photos into equal framed halves"); settle(editor);
  require(editor.editHistoryCurrentIndex() == before_layout_history + 1,
          "Layout, borders and the corrected canvas composition commit in one undo entry");
  const auto render = [&] {
    const auto parsed = parse_adjustment_payload_or_legacy_recipe(editor.currentAssetBoundAdjustmentPatchJson());
    require(parsed.ok(), "Read the board's persisted recipe");
    return render_flattened_output_file("framing-board", base_path, *parsed.state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32);
  };
  const auto initial = render();
  require(!initial.isNull(), "Render actual board pixels, including imported develop recipes");
  const auto proof_dir = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR");
  if (!proof_dir.isEmpty()) { QDir().mkpath(proof_dir); initial.save(QDir(proof_dir).filePath("framed-photos.png")); }
  for (const auto point : {QPoint(4, 20), QPoint(60, 4), QPoint(4, 60), QPoint(60, 74), QPoint(0, 20), QPoint(60, 40)})
    std::cout << "Frame pixel " << point.x() << ',' << point.y() << '=' << initial.pixelColor(point).name().toStdString() << '\n';
  const auto white = [](const QColor& color) { return color.red() > 245 && color.green() > 245 && color.blue() > 245; };
  require(white(initial.pixelColor(4, 20)) && white(initial.pixelColor(60, 4)) &&
          white(initial.pixelColor(4, 60)) && white(initial.pixelColor(60, 74)) &&
          white(initial.pixelColor(0, 20)) && white(initial.pixelColor(60, 40)) && white(initial.pixelColor(0, 0)),
          "The white layout backing reaches every edge and fills all margins and spacing");
  const auto developed = initial.pixelColor(60, 20);
  const auto edited_reference = render_flattened_output_file("edited-photo", photo_path, develop,
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32);
  const auto neutral_reference = render_flattened_output_file("unedited-photo", photo_path, EditRecipeState{},
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32);
  const auto expected = edited_reference.pixelColor(40, 120);
  require(qAbs(developed.red() - expected.red()) <= 1 && qAbs(developed.green() - expected.green()) <= 1 &&
          qAbs(developed.blue() - expected.blue()) <= 1 && developed.green() > neutral_reference.pixelColor(40, 120).green(),
          "The imported photo displays its saved exposure recipe, not the unedited original");
  const auto original_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto bounds = editor.photoLayerBounds(0);
  require(qAbs(bounds.value("width").toDouble() - 108) < .01 && qAbs(bounds.value("height").toDouble() - 31) < .01 &&
          qAbs(bounds.value("y").toDouble() - 6) < .6, "The top photo fills its exact half without stretching");
  require(editor.setBoardPhotoFraming(0, .5, 0, 1), "Reframe to the full source's top edge"); settle(editor);
  const auto top = render();
  require(top.pixelColor(60, 20).red() > top.pixelColor(60, 20).green() * 2 && white(top.pixelColor(4, 20)),
          "Reframing reveals previously hidden original pixels and leaves the border in place");
  require(editor.setBoardPhotoFraming(0, .5, 1, 2), "Reframe and zoom to the full source's bottom edge"); settle(editor);
  const auto bottom = render();
  require(bottom.pixelColor(60, 20).blue() > bottom.pixelColor(60, 20).red() * 2 &&
          white(bottom.pixelColor(4, 20)) && white(bottom.pixelColor(60, 4)),
          "Framing zoom retains the equal frame and the specified white border width");
  const auto reframed = editor.currentAssetBoundAdjustmentPatchJson();
  const auto layer = editor.photoLayers().front().toMap();
  require(layer.value("pixelWidth").toInt() == 80 && layer.value("pixelHeight").toInt() == 240 &&
          layer.value("sourceRecipeJson").toString() == source_recipe && QImage(photo_path) == photo,
          "All original pixels, saved develop settings and source identity survive framing");
  editor.undo(); settle(editor); editor.undo(); settle(editor);
  require(editor.currentAssetBoundAdjustmentPatchJson() == original_recipe, "Undo restores the complete initial framing and borders");
  editor.redo(); settle(editor); editor.redo(); settle(editor);
  require(editor.currentAssetBoundAdjustmentPatchJson() == reframed, "Redo restores the exact reframed recipe");
  EditorPreviewController reopened;
  reopened.loadAsset("reopened-framing-board", base_path, {}, {}, {}, {}, reframed, true); settle(reopened);
  const auto reopened_recipe = parse_adjustment_payload_or_legacy_recipe(reopened.currentAssetBoundAdjustmentPatchJson());
  const auto reopened_pixels = render_flattened_output_file("reopened-framing-board", base_path, *reopened_recipe.state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32);
  require(reopened_pixels == bottom, "Reopening preserves the developed photos, exact framing and visible borders");
  editor.undo(); settle(editor); editor.undo(); settle(editor); editor.undo(); settle(editor);
  require(editor.currentAssetBoundAdjustmentPatchJson() == before_layout,
          "One layout Undo restores the exact earlier canvas rendering and complete original photo layers");
  editor.redo(); settle(editor); editor.redo(); settle(editor); editor.redo(); settle(editor);
  require(editor.currentAssetBoundAdjustmentPatchJson() == reframed, "Redo restores the full corrected composition and framing");
  require(editor.applyBoardPhotoLayout({0, 1}, "stack", {{"margin", 0}, {"spacing", 0}, {"border", 4}, {"displayReferredCanvas", true}}),
          "Use a single uniform border width without extra margins or spacing"); settle(editor);
  const auto uniform = render();
  const auto white_run = [&](int start, int step) {
    int count = 0;
    for (int y = start; y >= 0 && y < uniform.height() && white(uniform.pixelColor(60, y)); y += step) ++count;
    return count;
  };
  require(white_run(0, 1) == 4 && white_run(79, -1) == 4 && white_run(38, 1) == 4 &&
          white(uniform.pixelColor(0, 0)) && white(uniform.pixelColor(119, 79)),
          "The top edge, bottom edge and shared center gap are each exactly four white pixels");
  int left = 0, right = 0;
  while (left < uniform.width() && white(uniform.pixelColor(left, 20))) ++left;
  while (right < uniform.width() && white(uniform.pixelColor(119 - right, 20))) ++right;
  require(left == 4 && right == 4 && editor.photoLayerCount() == 3,
          "All four outer edges match the shared divider, with only one background layer");
  const auto uniform_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  require(editor.photoLayerStyles(0).isEmpty() && editor.photoLayerStyles(1).isEmpty(),
          "Existing doubled photo strokes migrate to one shared white background");
  editor.undo(); settle(editor);
  require(editor.currentAssetBoundAdjustmentPatchJson() == reframed, "Undo restores the previous complete layout and white background");
  editor.redo(); settle(editor);
  require(editor.currentAssetBoundAdjustmentPatchJson() == uniform_recipe, "Redo restores exact shared-border geometry");
  QImage panorama(240, 80, QImage::Format_RGB32); panorama.fill(Qt::black);
  const auto panorama_path = runtime.filePath("layout-panorama.png");
  require(panorama.save(panorama_path), "Save a neighbouring-board background fixture");
  EditorPreviewController scoped;
  scoped.loadAsset("scoped-layout", panorama_path, {}, {}, {}, {}, {}, true); settle(scoped);
  require(scoped.addSourceLayer(photo_path) && scoped.addSourceLayer(photo_path) &&
          scoped.applyBoardPhotoLayout({0, 1}, "stack", {{"canvasX", 120}, {"canvasY", 0},
              {"canvasWidth", 120}, {"canvasHeight", 80}, {"border", 4}, {"displayReferredCanvas", true}}),
          "Arrange the right board of a shared canvas"); settle(scoped);
  const auto scoped_recipe = parse_adjustment_payload_or_legacy_recipe(scoped.currentAssetBoundAdjustmentPatchJson());
  const auto scoped_pixels = render_flattened_output_file("scoped-layout", panorama_path, *scoped_recipe.state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32);
  require(!scoped_pixels.isNull() && scoped_pixels.pixelColor(5, 5).value() < 5 &&
          scoped_pixels.pixelColor(119, 20).value() < 5 && white(scoped_pixels.pixelColor(120, 20)) &&
          white(scoped_pixels.pixelColor(239, 79)), "White margins fill their own board and leave its neighbour unchanged");
  if (!proof_dir.isEmpty()) { uniform.save(QDir(proof_dir).filePath("uniform-white-borders.png")); scoped_pixels.save(QDir(proof_dir).filePath("scoped-white-background.png")); }
  std::cout << "Photo import recipes, equal frames, visible borders, non-destructive reframe, Undo/Redo and reopen pixels passed\n";
}
