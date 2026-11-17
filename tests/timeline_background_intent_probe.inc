namespace {

QByteArray hash_file_sha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto chunk = file.read(1024 * 1024);
    if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return {};
    hash.addData(chunk);
  }
  return hash.result().toHex();
}

QStringList sorted_unique_ids(const QStringList& ids) {
  QSet<QString> unique;
  for (const auto& id : ids) {
    const auto trimmed = id.trimmed();
    if (!trimmed.isEmpty()) unique.insert(trimmed);
  }
  auto sorted = unique.values();
  sorted.sort();
  return sorted;
}

QJsonArray json_string_array(const QStringList& values) {
  QJsonArray result;
  for (const auto& value : values) result.append(value);
  return result;
}

bool invoke_background_intent_ids(QObject* helper, QStringList* ids, QString* error) {
  if (helper == nullptr || ids == nullptr) return false;
  QVariant result;
  if (!QMetaObject::invokeMethod(
          helper, "visibleBackgroundWorkClipIds", Q_RETURN_ARG(QVariant, result))) {
    if (error) *error = QStringLiteral("Could not invoke the production background-intent QML method.");
    return false;
  }
  if (result.canConvert<QJSValue>()) result = result.value<QJSValue>().toVariant();
  if (result.metaType().id() == QMetaType::QStringList) {
    *ids = result.toStringList();
    return true;
  }
  const auto values = result.toList();
  ids->clear();
  ids->reserve(values.size());
  for (const auto& value : values) ids->push_back(value.toString());
  return true;
}

bool run_timeline_background_intent_probe(const QStringList& arguments) {
  const auto value_for = [&arguments](const QString& flag) {
    const auto index = arguments.indexOf(flag);
    return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString{};
  };
  const QFileInfo project_info(value_for(QStringLiteral("--timeline-background-intent-probe")));
  const QFileInfo helper_info(value_for(QStringLiteral("--probe-qml-helper")));
  const QFileInfo output_info(value_for(QStringLiteral("--probe-output")));
  if (!project_info.isFile() || !project_info.isReadable() || project_info.isSymLink() ||
      project_info.suffix() != QStringLiteral("json") || project_info.size() <= 0 ||
      project_info.size() > 256LL * 1024LL * 1024LL ||
      helper_info.isFile() == false || helper_info.isReadable() == false ||
      helper_info.isSymLink() || output_info.fileName().isEmpty() || output_info.exists() ||
      output_info.isSymLink() || output_info.suffix() != QStringLiteral("json") ||
      !QFileInfo(output_info.absolutePath()).isDir()) {
    std::cerr << "Provide a readable native Timeline JSON project, the production QML helper, and a new JSON output path.\n";
    return false;
  }
  const auto project_path = project_info.canonicalFilePath();
  const auto helper_path = helper_info.canonicalFilePath();
  const auto source_hash_before = hash_file_sha256(project_path);
  if (project_path.isEmpty() || helper_path.isEmpty() || source_hash_before.isEmpty()) {
    std::cerr << "The Timeline probe could not canonicalize or hash its read-only inputs.\n";
    return false;
  }

  TimelineDocumentController controller;
  if (!controller.loadTimeline(project_path) || controller.clipCount() <= 0) {
    std::cerr << "The exact native Timeline could not be loaded: "
              << controller.status().toStdString() << '\n';
    return false;
  }
  const auto tracks = controller.timelineTracks();
  if (tracks.isEmpty()) {
    std::cerr << "The loaded native Timeline exposes no track projection.\n";
    return false;
  }

  QStringList preserved_ids = controller.selectedClipIds();
  const auto add_clip_id = [&preserved_ids](const QVariantMap& clip) {
    const auto id = clip.value(QStringLiteral("clipId")).toString().trimmed();
    if (!id.isEmpty() && !preserved_ids.contains(id)) preserved_ids.push_back(id);
  };
  add_clip_id(controller.currentClip());
  add_clip_id(controller.programClip());
  add_clip_id(controller.sourceClip());
  const auto playhead = controller.sequencePlayheadSeconds();
  for (const auto& track_value : tracks) {
    const auto track = track_value.toMap();
    const auto clips = track.value(QStringLiteral("clips")).toList();
    for (const auto& clip_value : clips) {
      const auto clip = clip_value.toMap();
      const auto start = clip.value(QStringLiteral("startSeconds")).toDouble();
      const auto end = clip.value(QStringLiteral("endSeconds")).toDouble();
      if (playhead >= start && playhead < end) add_clip_id(clip);
    }
  }
  preserved_ids = sorted_unique_ids(preserved_ids);

  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(helper_path));
  while (component.status() == QQmlComponent::Loading) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  std::unique_ptr<QObject> helper(component.create());
  if (!helper) {
    for (const auto& qml_error : component.errors())
      std::cerr << qml_error.toString().toStdString() << '\n';
    return false;
  }
  helper->setProperty("sequenceTracks", tracks);
  helper->setProperty("selectedClipIds", controller.selectedClipIds());
  helper->setProperty("activeProgramClipIds", preserved_ids);
  helper->setProperty("sourceClipId", controller.sourceClip().value(QStringLiteral("clipId")).toString());

  constexpr int kWindowSamples = 100;
  constexpr int kWarmupSamples = 5;
  constexpr int kVisibleTrackRows = 18;
  constexpr double kViewportWidth = 900.0;
  constexpr double kTimelineZoom = 80.0;
  const auto duration = std::max(0.0, controller.totalDurationSecondsValue());
  const auto last_content_x = std::max(0.0, duration * kTimelineZoom - kViewportWidth);
  const auto buffer = std::max(320.0, kViewportWidth * 0.75);
  QVector<qint64> samples_ns;
  samples_ns.reserve(kWindowSamples);
  QJsonArray windows;
  QSet<QString> full_baseline_union;
  QSet<QString> expected_candidate_union;
  qsizetype total_ids = 0;
  const auto configure_window = [&](int sample_index) {
    const int horizontal_slot = sample_index % 10;
    const int vertical_slot = sample_index / 10;
    const double content_x = last_content_x * static_cast<double>(horizontal_slot) / 9.0;
    const auto max_track_start = std::max<qsizetype>(
        0, tracks.size() - kVisibleTrackRows);
    const auto first_track = max_track_start * vertical_slot / 9;
    QVariantList visible_rows;
    for (auto row = first_track;
         row < std::min<qsizetype>(tracks.size(), first_track + kVisibleTrackRows);
         ++row)
      visible_rows.push_back(tracks.at(row));

    const double left_seconds = std::max(0.0, (content_x - buffer) / kTimelineZoom);
    const double right_seconds = std::min(
        duration, (content_x + kViewportWidth + buffer) / kTimelineZoom);
    helper->setProperty("visibleTrackRows", visible_rows);
    helper->setProperty("viewportWidth", kViewportWidth);
    helper->setProperty("timelineZoom", kTimelineZoom);
    helper->setProperty("viewportContentX", content_x);
    helper->setProperty("clipWindow", QVariantMap{
        {QStringLiteral("ready"), true},
        {QStringLiteral("left"), left_seconds},
        {QStringLiteral("right"), right_seconds},
    });
    return std::tuple{horizontal_slot, vertical_slot, content_x, left_seconds,
                      right_seconds, first_track, visible_rows};
  };

  // Warm the QML call path once before collecting 100 deterministic viewport
  // windows. The warmup uses a real projected track slice and the first x/y.
  configure_window(0);
  for (int warmup_index = 0; warmup_index < kWarmupSamples; ++warmup_index) {
    QStringList warmup_ids;
    QString invoke_error;
    if (!invoke_background_intent_ids(helper.get(), &warmup_ids, &invoke_error)) {
      std::cerr << invoke_error.toStdString() << '\n';
      return false;
    }
  }
  for (int sample_index = 0; sample_index < kWindowSamples; ++sample_index) {
    const auto [horizontal_slot, vertical_slot, content_x, left_seconds,
                right_seconds, first_track, visible_rows] = configure_window(sample_index);
    QElapsedTimer timer;
    timer.start();
    QStringList ids;
    QString invoke_error;
    if (!invoke_background_intent_ids(helper.get(), &ids, &invoke_error)) {
      std::cerr << invoke_error.toStdString() << '\n';
      return false;
    }
    samples_ns.push_back(timer.nsecsElapsed());
    const auto sorted_ids = sorted_unique_ids(ids);
    for (const auto& id : sorted_ids) full_baseline_union.insert(id);
    total_ids += sorted_ids.size();
    QSet<QString> vertically_materialized_clip_ids;
    for (const auto& track_value : visible_rows) {
      const auto clip_rows = track_value.toMap().value(QStringLiteral("clips")).toList();
      for (const auto& clip_value : clip_rows) {
        const auto id = clip_value.toMap().value(QStringLiteral("clipId")).toString().trimmed();
        if (!id.isEmpty()) vertically_materialized_clip_ids.insert(id);
      }
    }
    QSet<QString> expected_candidate_ids;
    for (const auto& id : preserved_ids) expected_candidate_ids.insert(id);
    for (const auto& id : sorted_ids) {
      if (vertically_materialized_clip_ids.contains(id)) expected_candidate_ids.insert(id);
    }
    QStringList sorted_expected_candidate_ids = expected_candidate_ids.values();
    sorted_expected_candidate_ids.sort();
    for (const auto& id : sorted_expected_candidate_ids)
      expected_candidate_union.insert(id);
    windows.append(QJsonObject{
        {QStringLiteral("sampleIndex"), sample_index},
        {QStringLiteral("horizontalSlot"), horizontal_slot},
        {QStringLiteral("verticalSlot"), vertical_slot},
        {QStringLiteral("firstVisibleTrackIndex"), static_cast<qint64>(first_track)},
        {QStringLiteral("visibleTrackCount"), static_cast<qint64>(visible_rows.size())},
        {QStringLiteral("viewportContentX"), content_x},
        {QStringLiteral("viewportWidth"), kViewportWidth},
        {QStringLiteral("timelineZoom"), kTimelineZoom},
        {QStringLiteral("clipWindowLeftSeconds"), left_seconds},
        {QStringLiteral("clipWindowRightSeconds"), right_seconds},
        {QStringLiteral("returnedClipIds"), json_string_array(sorted_ids)},
        {QStringLiteral("expectedVisibleRowPlusPriorityClipIds"),
         json_string_array(sorted_expected_candidate_ids)},
    });
  }
  if (samples_ns.size() != kWindowSamples) {
    std::cerr << "The baseline probe did not collect every requested window sample.\n";
    return false;
  }
  std::sort(samples_ns.begin(), samples_ns.end());
  const auto percentile_ns = [&samples_ns](double percentile) {
    const auto index = static_cast<qsizetype>(std::ceil(
        percentile * static_cast<double>(samples_ns.size()))) - 1;
    return samples_ns.at(std::clamp<qsizetype>(index, 0, samples_ns.size() - 1));
  };
  const auto source_hash_after = hash_file_sha256(project_path);
  if (source_hash_after != source_hash_before) {
    std::cerr << "The read-only background-intent probe changed its native project input.\n";
    return false;
  }

  QStringList baseline_union = full_baseline_union.values();
  baseline_union.sort();
  QStringList expected_union = expected_candidate_union.values();
  expected_union.sort();
  const auto baseline_union_hash = QCryptographicHash::hash(
      baseline_union.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex();
  const auto expected_union_hash = QCryptographicHash::hash(
      expected_union.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex();
  QStringList preservation_ids = preserved_ids;
  preservation_ids.sort();
  const auto preservation_hash = QCryptographicHash::hash(
      preservation_ids.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex();
  const QJsonObject report{
      {QStringLiteral("schema"), QStringLiteral("timeline-background-intent-baseline-v1")},
      {QStringLiteral("qualification"), QStringLiteral(
          "Production QML clip-window and all-track background-intent function called against the exact saved native controller projection; component CPU-side latency, not whole-UI frame time.")},
      {QStringLiteral("projectPath"), project_path},
      {QStringLiteral("projectSha256"), QString::fromLatin1(source_hash_before)},
      {QStringLiteral("qmlHelperPath"), helper_path},
      {QStringLiteral("timelineTrackCount"), static_cast<qint64>(tracks.size())},
      {QStringLiteral("timelineClipCount"), controller.clipCount()},
      {QStringLiteral("durationSeconds"), duration},
      {QStringLiteral("sequencePlayheadSeconds"), playhead},
      {QStringLiteral("selectedClipIds"), json_string_array(sorted_unique_ids(controller.selectedClipIds()))},
      {QStringLiteral("preservedSelectedSourceAndPlayheadActiveClipIds"), json_string_array(preservation_ids)},
      {QStringLiteral("preservationIdSha256"), QString::fromLatin1(preservation_hash)},
      {QStringLiteral("sampleCount"), static_cast<qint64>(samples_ns.size())},
      {QStringLiteral("warmupCount"), kWarmupSamples},
      {QStringLiteral("viewportWidth"), kViewportWidth},
      {QStringLiteral("timelineZoom"), kTimelineZoom},
      {QStringLiteral("visibleTrackRowsPerWindow"), kVisibleTrackRows},
      {QStringLiteral("baselineReturnedClipCountAcrossSamples"), static_cast<qint64>(total_ids)},
      {QStringLiteral("baselineUniqueClipIdsAcrossSamples"),
       static_cast<qint64>(baseline_union.size())},
      {QStringLiteral("baselineUnionClipIdSha256"), QString::fromLatin1(baseline_union_hash)},
      {QStringLiteral("expectedCandidateUniqueClipIdsAcrossSamples"),
       static_cast<qint64>(expected_union.size())},
      {QStringLiteral("expectedCandidateUnionClipIdSha256"), QString::fromLatin1(expected_union_hash)},
      {QStringLiteral("latencyP50Milliseconds"), static_cast<double>(percentile_ns(0.50)) / 1.0e6},
      {QStringLiteral("latencyP95Milliseconds"), static_cast<double>(percentile_ns(0.95)) / 1.0e6},
      {QStringLiteral("latencyMaximumMilliseconds"), static_cast<double>(samples_ns.constLast()) / 1.0e6},
      {QStringLiteral("perWindowSamples"), windows},
  };
  const auto payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
  QSaveFile output(output_info.absoluteFilePath());
  if (!output.open(QIODevice::WriteOnly) || output.write(payload) != payload.size() || !output.commit()) {
    std::cerr << "Could not publish the new background-intent baseline artifact.\n";
    return false;
  }
  std::cout << payload.constData();
  return true;
}

}  // namespace
