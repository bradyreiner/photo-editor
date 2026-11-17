QJsonObject timeline_keyframe_projection_graph(const QJsonObject& document) {
  QJsonArray tracks;
  for (const auto& track_value : document.value(QStringLiteral("tracks")).toArray()) {
    const auto track = track_value.toObject();
    QJsonArray clips;
    for (const auto& clip_value : track.value(QStringLiteral("clips")).toArray()) {
      const auto clip = clip_value.toObject();
      QJsonObject variants;
      const auto raw_variants = clip.value(QStringLiteral("outputVariantOverrides")).toObject();
      for (auto variant = raw_variants.begin(); variant != raw_variants.end(); ++variant) {
        const auto variant_value = variant.value().toObject();
        variants.insert(
            variant.key(),
            QJsonObject{{QStringLiteral("framingKeyframes"),
                         variant_value.value(QStringLiteral("framingKeyframes"))}});
      }
      clips.push_back(QJsonObject{
          {QStringLiteral("clipId"), clip.value(QStringLiteral("clipId"))},
          {QStringLiteral("keyframes"), clip.value(QStringLiteral("keyframes"))},
          {QStringLiteral("outputVariantOverrides"), variants}});
    }
    tracks.push_back(QJsonObject{
        {QStringLiteral("trackId"), track.value(QStringLiteral("trackId"))},
        {QStringLiteral("clips"), clips}});
  }
  return QJsonObject{{QStringLiteral("tracks"), tracks}};
}

qsizetype timeline_keyframe_count(const QJsonObject& graph) {
  qsizetype count = 0;
  for (const auto& track_value : graph.value(QStringLiteral("tracks")).toArray()) {
    for (const auto& clip_value : track_value.toObject().value(QStringLiteral("clips")).toArray()) {
      count += clip_value.toObject().value(QStringLiteral("keyframes")).toArray().size();
      const auto variants = clip_value.toObject()
                                .value(QStringLiteral("outputVariantOverrides")).toObject();
      for (auto variant = variants.begin(); variant != variants.end(); ++variant) {
        count += variant.value().toObject()
                     .value(QStringLiteral("framingKeyframes")).toArray().size();
      }
    }
  }
  return count;
}

bool run_timeline_keyframe_json_cache_checkpoint_probe(const QStringList& arguments) {
  const auto option_index = arguments.indexOf(
      QStringLiteral("--timeline-keyframe-json-cache-checkpoint"));
  if (option_index < 0 || option_index + 2 >= arguments.size()) {
    std::cerr << "checkpoint probe requires an exact saved project path and SHA-256\n";
    return false;
  }
  const auto input_path = arguments.at(option_index + 1);
  const auto expected_sha = arguments.at(option_index + 2).toLatin1();
  QFile source_file(input_path);
  if (expected_sha.size() != 64 || !source_file.open(QIODevice::ReadOnly)) {
    std::cerr << "checkpoint probe source could not be opened or SHA-256 was malformed\n";
    return false;
  }
  const auto source_bytes = source_file.readAll();
  if (source_file.error() != QFileDevice::NoError ||
      sha256_hex(source_bytes).toLatin1() != expected_sha) {
    std::cerr << "checkpoint probe source bytes did not match the supplied SHA-256\n";
    return false;
  }
  QJsonParseError parse_error;
  const auto source_document = QJsonDocument::fromJson(source_bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !source_document.isObject()) {
    std::cerr << "checkpoint probe source was not a Timeline JSON object\n";
    return false;
  }

  TimelineDocumentController controller;
  controller.setEditAccessPolicy([] { return true; });
  if (!controller.loadTimeline(input_path)) {
    std::cerr << "checkpoint probe could not load the saved Timeline\n";
    return false;
  }
  photo_editor::desktop::TimelineHistoryTestAccess::resetKeyframeJsonCaches(controller);

  QElapsedTimer cold_timer;
  cold_timer.start();
  const auto cold_document =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller);
  const auto cold_elapsed_ms = cold_timer.elapsed();

  QElapsedTimer warm_timer;
  warm_timer.start();
  const auto warm_document =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller);
  const auto warm_elapsed_ms = warm_timer.elapsed();

  const auto source_graph = timeline_keyframe_projection_graph(source_document.object());
  const auto cold_graph = timeline_keyframe_projection_graph(cold_document);
  const auto warm_graph = timeline_keyframe_projection_graph(warm_document);
  const auto source_graph_bytes = QJsonDocument(source_graph).toJson(QJsonDocument::Compact);
  const auto cold_graph_bytes = QJsonDocument(cold_graph).toJson(QJsonDocument::Compact);
  const auto warm_graph_bytes = QJsonDocument(warm_graph).toJson(QJsonDocument::Compact);
  const auto cold_document_bytes = QJsonDocument(cold_document).toJson(QJsonDocument::Compact);
  const auto warm_document_bytes = QJsonDocument(warm_document).toJson(QJsonDocument::Compact);
  const auto source_graph_sha = sha256_hex(source_graph_bytes);
  const auto cold_graph_sha = sha256_hex(cold_graph_bytes);
  const auto warm_graph_sha = sha256_hex(warm_graph_bytes);
  const auto cold_document_sha = sha256_hex(cold_document_bytes);
  const auto warm_document_sha = sha256_hex(warm_document_bytes);
  const auto exact_keyframe_graph_match = source_graph_bytes == cold_graph_bytes &&
      source_graph_bytes == warm_graph_bytes;
  // Serialization stamps updatedAt on each call. Require every other field,
  // including all authored values, to remain byte-identical.
  auto normalized_warm_document = warm_document;
  normalized_warm_document.insert(QStringLiteral("updatedAt"),
                                  cold_document.value(QStringLiteral("updatedAt")));
  const auto exact_cold_warm_document_match = cold_document_bytes ==
      QJsonDocument(normalized_warm_document).toJson(QJsonDocument::Compact);
  QJsonObject report{
      {QStringLiteral("checkpointSha256"), QString::fromLatin1(expected_sha)},
      {QStringLiteral("keyframeProjectionGraphSha256"), source_graph_sha},
      {QStringLiteral("coldGraphSha256"), cold_graph_sha},
      {QStringLiteral("warmGraphSha256"), warm_graph_sha},
      {QStringLiteral("exactKeyframeGraphMatch"), exact_keyframe_graph_match},
      {QStringLiteral("coldSerializedDocumentSha256"), cold_document_sha},
      {QStringLiteral("warmSerializedDocumentSha256"), warm_document_sha},
      {QStringLiteral("coldWarmDocumentMatchExceptUpdatedAt"),
       exact_cold_warm_document_match},
      {QStringLiteral("keyframeAndFramingKeyframeCount"),
       static_cast<double>(timeline_keyframe_count(source_graph))},
      {QStringLiteral("coldSerializeMilliseconds"), static_cast<double>(cold_elapsed_ms)},
      {QStringLiteral("warmSerializeMilliseconds"), static_cast<double>(warm_elapsed_ms)},
      {QStringLiteral("resourceId"), controller.inspectAgentDocument()
           .value(QStringLiteral("resourceId")).toString()},
      {QStringLiteral("documentHash"), controller.inspectAgentDocument()
           .value(QStringLiteral("documentHash")).toString()},
  };
  std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).toStdString() << '\n';
  if (!exact_keyframe_graph_match || !exact_cold_warm_document_match) {
    std::cerr << "checkpoint probe found a keyframe or output-variant serialization mismatch\n";
  }
  return exact_keyframe_graph_match && exact_cold_warm_document_match;
}
