  if (qEnvironmentVariableIsSet("PHOTO_FILM_STOCK_WORKFLOW_ONLY")) {
    const auto stocks = controller->availableFilmGrainStocks();
    CHECK(stocks.size() == 6, "Six distinct stock families are exposed");
    CHECK(controller->exportFlattenedPhoto(temp.filePath("grain-baseline.png")), "Export baseline");
    CHECK(wait_until([&]{ return !controller->photoExportBusy(); }) && controller->photoExportError().isEmpty(), "Baseline export completes");
    const QImage baseline(temp.filePath("grain-baseline.png"));
    if (!evidence.isEmpty()) CHECK(baseline.save(evidence + "/baseline.png"), "Baseline evidence");
    const auto send_key = [&](int key) {
      QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
      QCoreApplication::sendEvent(&window, &down);
      QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
      QCoreApplication::sendEvent(&window, &up);
      settle();
    };
    for (int stock_index = 0; stock_index < stocks.size(); ++stock_index) {
      CHECK(controller->photoLayerEffects(0).isEmpty(), "Each stock starts isolated");
      CHECK(click(find("photoAddEffectButton")), "Open film grain discovery");
      auto* effect_picker = find("photoEffectPicker");
      CHECK(wait_until([&]{return effect_picker->property("opened").toBool();}), "Effect picker opens fully");
      auto* search = find("photoEffectSearchField"); CHECK(search, "Effect search exists");
      search->setProperty("text", QStringLiteral("film grain")); settle();
      CHECK(click(find("photoEffectChoice_film_grain")), "Choose grain through native pointer events");
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}), "Default grain preview settles");
      auto rows = controller->photoLayerEffects(0);
      CHECK(rows.size() == 1 && rows[0].toMap().value("grainStock").toString() == "portrait_400", "New grain uses the portrait stock renderer");
      auto* picker = find("filmGrainStockPicker"); CHECK(picker, "Stock picker exists");
      CHECK(click(picker), "Open stock picker with pointer");
      auto* popup = picker->property("popup").value<QObject*>();
      CHECK(popup && wait_until([&]{return popup->property("opened").toBool();}), "Stock menu opens");
      send_key(Qt::Key_Home);
      for (int row = 0; row <= stock_index; ++row) send_key(Qt::Key_Down);
      send_key(Qt::Key_Return);
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}), "Selected stock preview settles");
      rows = controller->photoLayerEffects(0);
      const auto selected = rows[0].toMap();
      const auto stock = stocks[stock_index].toMap();
      const auto stock_id = stock.value("grainStock").toString();
      CHECK(selected.value("grainStock") == stock.value("grainStock"), "Native keyboard selection applies the requested stock");
      CHECK(selected.value("grainRoughness") == stock.value("grainRoughness"), "Stock loads its structure defaults atomically");
      if (!evidence.isEmpty()) CHECK(window.grabWindow().save(evidence + "/" + stock_id + "-controls.png"), "Native stock controls evidence");
      CHECK(click(find("photoEffectPreviewApply")), "Apply stock through native pointer");
      CHECK(wait_until([&]{return !effect_picker->property("visible").toBool();}), "Apply closes effect popup");
      const auto expected = controller->photoLayerEffects(0);
      controller->undo(); CHECK(controller->photoLayerEffects(0).isEmpty(), "One undo removes the entire stock preview transaction");
      controller->redo(); CHECK(controller->photoLayerEffects(0) == expected, "Redo restores stock and every control");
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}), "Redo preview ready");
      const auto export_path = temp.filePath(stock_id + ".png");
      CHECK(controller->exportFlattenedPhoto(export_path), "Export stock");
      CHECK(wait_until([&]{return !controller->photoExportBusy();}) && controller->photoExportError().isEmpty(), "Stock PNG export completes");
      const QImage rendered(export_path);
      CHECK(!rendered.isNull() && rendered.size() == source.size() && rendered != baseline, "Stock changes the full-resolution photo");
      const auto state = EditorPreviewControllerTestAccess::state(*controller);
      const auto recipe = serialize_edit_recipe_json(state);
      auto reopened = std::make_unique<EditorPreviewController>();
      reopened->loadAsset("grain-reopened", source_path, {}, {}, {}, {}, recipe);
      CHECK(reopened->photoLayerEffects(0) == expected, "Recipe reopen retains all stock parameters");
      CHECK(reopened->exportFlattenedPhoto(temp.filePath("grain-reopened.png")), "Export reopened stock");
      CHECK(wait_until([&]{return !reopened->photoExportBusy();}) && reopened->photoExportError().isEmpty(), "Reopened export completes");
      CHECK(rendered == QImage(temp.filePath("grain-reopened.png")), "Reopened stock export is pixel identical");
      if (!evidence.isEmpty()) {
        CHECK(rendered.save(evidence + "/" + stock_id + ".png"), "Stock photograph evidence");
        QFile file(evidence + "/" + stock_id + ".json"); CHECK(file.open(QIODevice::WriteOnly), "Stock recipe file"); file.write(recipe.toUtf8());
      }
      controller->undo(); CHECK(controller->photoLayerEffects(0).isEmpty(), "Clear stock for next review");
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}), "Undo render settles");
      std::cout << "Verified native stock selection, one-step undo/redo, recipe reopen and exact PNG export: " << stock_id.toStdString() << '\n';
    }
    // Verify explicit edits are preserved, and invalid stock IDs cannot mutate history.
    CHECK(controller->setPhotoLayerEffectParameters(0, "film_grain", {}, {{"grainStock", "classic_400"},
        {"grainRoughness", .72}, {"grainShadows", .4}, {"grainHighlights", 1.35}, {"seed", 123456}}, 1, true), "Custom stock controls");
    const auto customized = controller->photoLayerEffects(0);
    const auto custom_id = customized[0].toMap().value("id").toString();
    CHECK(!controller->setPhotoLayerEffectParameters(0, "film_grain", custom_id, {{"grainStock", "invalid"}}, 1, true)
        && controller->photoLayerEffects(0) == customized, "Invalid stock is rejected without changing state");
    auto reopened = std::make_unique<EditorPreviewController>();
    reopened->loadAsset("custom-grain", source_path, {}, {}, {}, {}, serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(*controller)));
    CHECK(reopened->photoLayerEffects(0) == customized, "Customized grain structure, tone and seed survive reopen");
    std::cout << "Film stock native workflow passed; photographic=" << photographic << '\n';
    return 0;
  }
