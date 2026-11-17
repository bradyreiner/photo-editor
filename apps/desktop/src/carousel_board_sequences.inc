QVariantMap CarouselDocumentController::boardSequence(int index) const {
  if (index < 0 || index >= slides_.size()) return {};
  auto result = document_passthrough_.value(QStringLiteral("boardSequences")).toObject()
      .value(slides_.at(index).edit_board_id).toObject().toVariantMap();
  if (!result.isEmpty()) {
    result.insert(QStringLiteral("videoUrl"), QUrl::fromLocalFile(result.value(QStringLiteral("videoPath")).toString()).toString());
    result.insert(QStringLiteral("posterUrl"), QUrl::fromLocalFile(result.value(QStringLiteral("posterPath")).toString()).toString());
  }
  return result;
}

QVariantMap CarouselDocumentController::prepareBoardSequence(int index) {
  const auto access = customer_edit_access_.enter();
  if (!access || export_busy_ || index < 0 || index >= slides_.size() || !prepareEditBoards()) return {};
  if (groupForIndex(index)) { setStatus(QStringLiteral("Add an independent blank board for its own video sequence.")); return {}; }
  if (!saveDocument()) return {};
  const auto board_id = slides_.at(index).edit_board_id;
  const auto directory = QDir(carousel_documents_root()).filePath(
      QStringLiteral("sequences/%1/%2").arg(saved_document_id_, board_id));
  if (!QDir().mkpath(directory)) return {};
  const auto revision = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto background = QDir(directory).filePath(revision + QStringLiteral("-canvas.png"));
  auto result = boardSequence(index);
  auto decoded = parse_adjustment_payload_or_legacy_recipe(slides_.at(index).primary.adjustment_patch_json);
  if (!decoded.ok()) { setStatus(QStringLiteral("The board layers could not be read.")); return {}; }
  auto layers = decoded.state->layers;
  QVariantList video_layers;
  for (const auto& value : layers) if (!value.toMap().value("boardVideoSource").toMap().isEmpty()) video_layers.push_back(value);
  // Older board sequences used one flattened background. Admit their video
  // clips as editable layers on the next Apply, preserving their saved trims.
  if (video_layers.isEmpty() && !result.value("sequencePath").toString().isEmpty()) {
    QFile previous(result.value("sequencePath").toString());
    if (previous.open(QIODevice::ReadOnly)) {
      const auto object = QJsonDocument::fromJson(previous.readAll()).object();
      for (const auto& track_value : object.value("tracks").toArray()) for (const auto& clip_value : track_value.toObject().value("clips").toArray()) {
        const auto clip = clip_value.toObject().toVariantMap();
        if (clip.value("mediaType").toString() != "video") continue;
        auto layer = board_video_source_layer(clip.value("filePath").toString(), clip.value("displayLabel").toString(),
            clip.value("assetId").toString(), clip.value("trimStartSeconds").toDouble());
        if (layer.isEmpty()) { setStatus(QStringLiteral("Reconnect the board's original video in Library.")); return {}; }
        layer = board_video_layer_from_clip(layer, clip, exportPixelSize());
        auto source = layer.value("boardVideoSource").toMap(); source.insert("clipId", clip.value("clipId"));
        layer.insert("boardVideoSource", source);
        layers.prepend(layer); video_layers.push_back(layer);
      }
    }
  }
  QVariantList composition;
  QImage image;
  if (video_layers.isEmpty()) image = renderOriginalSlideImage(index, exportPixelSize(), false);
  else {
    CarouselDocumentController snapshot;
    if (!snapshot.loadDocumentObject(persistentDocumentObject())) return {};
    auto state = *decoded.state; state.layers.clear();
    snapshot.slides_[index].primary.adjustment_patch_json = serialize_adjustment_patch_json(state, decoded.groups, AdjustmentPatchScope::AssetBound);
    snapshot.slides_[index].primary.recipe_state = state;
    image = snapshot.renderSlideBaseImage(index, exportPixelSize(), false);
    composition.push_back(QVariantMap{{"kind", "photo"}, {"key", "base"}, {"filePath", background}, {"name", "Canvas background"}});
    const auto transparent = QDir(directory).filePath(revision + "-transparent.png");
    QImage clear(exportPixelSize(), QImage::Format_ARGB32_Premultiplied); clear.fill(Qt::transparent);
    if (!clear.save(transparent, "PNG")) return {};
    snapshot.slides_[index].primary.file_path = transparent;
    snapshot.slides_[index].background_kind = "transparent";
    snapshot.items_.clear();
    QVariantList static_layers;
    int part = 0;
    const auto flush_static = [&]() {
      if (static_layers.isEmpty()) return true;
      state.layers = static_layers;
      snapshot.slides_[index].primary.adjustment_patch_json = serialize_adjustment_patch_json(state, decoded.groups, AdjustmentPatchScope::AssetBound);
      snapshot.slides_[index].primary.recipe_state = state;
      const auto path = QDir(directory).filePath(revision + QStringLiteral("-layers-%1.png").arg(part));
      const auto overlay = snapshot.renderOriginalSlideImage(index, exportPixelSize(), false);
      if (overlay.isNull() || !overlay.save(path, "PNG")) return false;
      composition.push_back(QVariantMap{{"kind", "photo"}, {"key", QStringLiteral("static-%1").arg(part++)},
          {"filePath", path}, {"name", "Canvas layers"}});
      static_layers.clear(); return true;
    };
    // Native layers are top first; Timeline picture lanes composite bottom first.
    for (auto layer = layers.crbegin(); layer != layers.crend(); ++layer) {
      if (layer->toMap().value("boardVideoSource").toMap().isEmpty()) static_layers.prepend(*layer);
      else {
        if (!flush_static()) return {};
        composition.push_back(QVariantMap{{"kind", "video"}, {"key", layer->toMap().value("id")}, {"layer", *layer}});
      }
    }
    if (!flush_static()) return {};
    const auto overlay = renderSlideOverlayImage(index, exportPixelSize());
    if (!items_.isEmpty()) {
      const auto path = QDir(directory).filePath(revision + "-graphics.png");
      if (!overlay.save(path, "PNG")) return {};
      composition.push_back(QVariantMap{{"kind", "photo"}, {"key", "graphics"}, {"filePath", path}, {"name", "Board graphics"}});
    }
  }
  QSaveFile output(background);
  if (image.isNull() || !output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG") || !output.commit()) {
    setStatus(QStringLiteral("The canvas background could not be prepared for Timeline.")); return {};
  }
  result.insert(QStringLiteral("videoLayers"), video_layers);
  result.insert(QStringLiteral("composition"), composition);
  result.insert(QStringLiteral("sourceBoardId"), result.value(QStringLiteral("boardId")));
  result.insert(QStringLiteral("documentId"), saved_document_id_);
  result.insert(QStringLiteral("boardId"), board_id);
  result.insert(QStringLiteral("name"), slides_.at(index).board_name);
  result.insert(QStringLiteral("directory"), directory);
  result.insert(QStringLiteral("backgroundPath"), background);
  result.insert(QStringLiteral("width"), exportPixelSize().width());
  result.insert(QStringLiteral("height"), exportPixelSize().height());
  result.insert(QStringLiteral("projectName"), projectName());
  result.insert(QStringLiteral("canvasPatchHash"), QString::fromLatin1(QCryptographicHash::hash(
      (slides_.at(index).primary.file_path + slides_.at(index).primary.adjustment_patch_json).toUtf8(), QCryptographicHash::Sha256).toHex()));
  return result;
}

bool CarouselDocumentController::attachBoardSequence(const QString& document_id, const QString& board_id,
                                                     const QVariantMap& sequence) {
  const auto access = customer_edit_access_.enter();
  if (!access || export_busy_ || document_id != saved_document_id_) return false;
  const auto found = std::find_if(slides_.cbegin(), slides_.cend(), [&board_id](const auto& slide) { return slide.edit_board_id == board_id; });
  if (found == slides_.cend()) { setStatus(QStringLiteral("The target board is no longer available.")); return false; }
  const auto board_index = static_cast<int>(std::distance(slides_.cbegin(), found));
  const auto current_patch_hash = QString::fromLatin1(QCryptographicHash::hash(
      (found->primary.file_path + found->primary.adjustment_patch_json).toUtf8(), QCryptographicHash::Sha256).toHex());
  if (sequence.value(QStringLiteral("canvasPatchHash")).toString() != current_patch_hash) {
    setStatus(QStringLiteral("The canvas background changed while editing the sequence. Reopen it to refresh the background.")); return false;
  }
  for (const auto* key : {"sequencePath", "videoPath", "posterPath"})
    if (!QFileInfo(sequence.value(QLatin1String(key)).toString()).isFile()) {
      setStatus(QStringLiteral("The sequence, video, and poster must finish saving before applying to this board.")); return false;
    }
  if (sequence.value(QStringLiteral("width")).toInt() != exportPixelSize().width() ||
      sequence.value(QStringLiteral("height")).toInt() != exportPixelSize().height()) {
    setStatus(QStringLiteral("Keep the sequence resolution matched to the carousel board.")); return false;
  }
  pushUndoState(QStringLiteral("Apply board video sequence"));
  const auto applied_layers = sequence.value("videoLayers").toList();
  if (!applied_layers.isEmpty() || sequence.value("usesBoardLayers").toBool()) {
    auto decoded = parse_adjustment_payload_or_legacy_recipe(slides_.at(board_index).primary.adjustment_patch_json);
    if (!decoded.ok()) return false;
    auto layers = decoded.state->layers;
    QSet<QString> applied_ids;
    for (const auto& value : applied_layers) applied_ids.insert(value.toMap().value("id").toString());
    for (qsizetype i = layers.size(); i > 0; --i) {
      const auto layer = layers.at(i - 1).toMap();
      if (!layer.value("boardVideoSource").toMap().isEmpty() && !applied_ids.contains(layer.value("id").toString())) layers.removeAt(i - 1);
    }
    for (const auto& value : applied_layers) {
      const auto id = value.toMap().value("id").toString();
      const auto existing = std::find_if(layers.begin(), layers.end(), [&id](const auto& candidate) { return candidate.toMap().value("id").toString() == id; });
      if (existing == layers.end()) layers.prepend(value); else *existing = value;
    }
    decoded.state->layers = layers;
    slides_[board_index].primary.adjustment_patch_json = serialize_adjustment_patch_json(*decoded.state, decoded.groups, AdjustmentPatchScope::AssetBound);
    slides_[board_index].primary.recipe_state = *decoded.state;
  }
  auto sequences = document_passthrough_.value(QStringLiteral("boardSequences")).toObject();
  auto saved = QJsonObject::fromVariantMap(sequence);
  saved.remove("composition"); saved.remove("videoLayers"); saved.remove("clipLayerIds");
  saved.insert("canvasPatchHash", QString::fromLatin1(QCryptographicHash::hash(
      (slides_.at(board_index).primary.file_path + slides_.at(board_index).primary.adjustment_patch_json).toUtf8(), QCryptographicHash::Sha256).toHex()));
  saved.insert(QStringLiteral("needsUpdate"), false);
  sequences.insert(board_id, saved);
  document_passthrough_.insert(QStringLiteral("boardSequences"), sequences);
  refreshPreviews();
  emitCanvasStateChanged(true);
  return saveDocument();
}
