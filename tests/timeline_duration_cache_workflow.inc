bool run_timeline_duration_cache_workflow(const QString& workspace_path) {
  const auto check = [](bool condition, const char* message) {
    if (!condition) {
      std::cerr << message << '\n';
    }
    return condition;
  };
  auto duration_observed_during_clips_changed = -1.0;
  auto clips_changed_observer_calls = 0;
  TimelineDocumentController controller;
  const auto duration_connection = QObject::connect(
      &controller,
      &TimelineDocumentController::clipsChanged,
      &controller,
      [&]() {
        ++clips_changed_observer_calls;
        duration_observed_during_clips_changed =
            controller.totalDurationSecondsValue();
      },
      Qt::DirectConnection);
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Duration cache workflow")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 30.0},
  });

  const auto expect_duration = [&](double expected, const char* message) {
    const auto current = controller.totalDurationSecondsValue();
    if (!nearly_equal(current, expected) ||
        !nearly_equal(duration_observed_during_clips_changed, expected)) {
      std::cerr << message << " current=" << current
                << " observer=" << duration_observed_during_clips_changed
                << " expected=" << expected << '\n';
      return false;
    }
    return true;
  };

  if (!controller.addTitleClipFromTemplate(QStringLiteral("basic")) ||
      !expect_duration(3.0, "Added title must publish fresh duration")) {
    QObject::disconnect(duration_connection);
    return false;
  }
  auto scans = photo_editor::desktop::TimelineHistoryTestAccess::
      totalDurationCacheScanCount(controller);
  for (auto index = 0; index < 1000; ++index) {
    if (!nearly_equal(controller.totalDurationSecondsValue(), 3.0)) {
      QObject::disconnect(duration_connection);
      return check(false, "Repeated duration property reads changed the result");
    }
  }
  if (!check(photo_editor::desktop::TimelineHistoryTestAccess::
                 totalDurationCacheScanCount(controller) == scans,
             "Repeated duration property reads should reuse the aggregate cache")) {
    QObject::disconnect(duration_connection);
    return false;
  }
  const auto first_clip_id = controller.currentClip()
                                 .value(QStringLiteral("clipId"))
                                 .toString();
  photo_editor::desktop::TimelineHistoryTestAccess::setStatusOnlyDisplayLabel(
      controller, first_clip_id, QStringLiteral("Updated display metadata"));
  const auto scans_before_metadata_signal =
      photo_editor::desktop::TimelineHistoryTestAccess::
          totalDurationCacheScanCount(controller);
  photo_editor::desktop::TimelineHistoryTestAccess::notifyClipsChanged(controller);
  if (!expect_duration(3.0,
                       "A direct clipsChanged observer must read fresh duration") ||
      !check(photo_editor::desktop::TimelineHistoryTestAccess::
                     totalDurationCacheScanCount(controller) ==
                 scans_before_metadata_signal + 1,
             "A status-only clipsChanged signal should trigger one aggregate scan")) {
    QObject::disconnect(duration_connection);
    return false;
  }

  controller.seekSequenceSeconds(3.0);
  if (!controller.addTitleClipFromTemplate(QStringLiteral("basic")) ||
      !expect_duration(6.0, "Adding a second title must extend duration")) {
    QObject::disconnect(duration_connection);
    return false;
  }
  if (!controller.moveSelectedClipToTrackRowWithMode(
          0, 8.0, QStringLiteral("insert"), false) ||
      !expect_duration(11.0, "Moving a clip later must refresh duration before observers")) {
    QObject::disconnect(duration_connection);
    return false;
  }
  controller.trimSelectedClipEndToSequenceSeconds(10.0, false);
  if (!expect_duration(10.0, "Trimming a clip must refresh duration before observers")) {
    QObject::disconnect(duration_connection);
    return false;
  }

  const auto moved_clip_id = controller.currentClip()
                                 .value(QStringLiteral("clipId"))
                                 .toString();
  if (moved_clip_id.isEmpty() ||
      !photo_editor::desktop::TimelineHistoryTestAccess::setClipMediaType(
          controller, moved_clip_id, QStringLiteral("video")) ||
      !controller.addSelectedClipRetimeHold(1.0, 1.0) ||
      !expect_duration(11.0, "Retime hold must extend duration before observers")) {
    QObject::disconnect(duration_connection);
    return check(false, "The duration retime fixture should be editable");
  }
  controller.undo();
  if (!expect_duration(10.0, "Undo must restore the prior duration")) {
    QObject::disconnect(duration_connection);
    return false;
  }
  controller.redo();
  if (!expect_duration(11.0, "Redo must restore the retimed duration")) {
    QObject::disconnect(duration_connection);
    return false;
  }
  controller.removeSelectedClip();
  if (!expect_duration(3.0, "Deleting the longest clip must shorten duration")) {
    QObject::disconnect(duration_connection);
    return false;
  }
  controller.undo();
  if (!expect_duration(11.0, "Undo after deletion must restore duration")) {
    QObject::disconnect(duration_connection);
    return false;
  }
  controller.redo();
  if (!expect_duration(3.0, "Redo after deletion must restore shortened duration")) {
    QObject::disconnect(duration_connection);
    return false;
  }

  const auto remaining_clip_id = controller.clips().value(0).toMap()
                                     .value(QStringLiteral("clipId"))
                                     .toString();
  if (!photo_editor::desktop::TimelineHistoryTestAccess::setClipStart(
          controller, remaining_clip_id, 5.0)) {
    QObject::disconnect(duration_connection);
    return check(false, "The batch invalidation fixture should contain a clip");
  }
  photo_editor::desktop::TimelineHistoryTestAccess::setMutationBatchActive(
      controller, true);
  photo_editor::desktop::TimelineHistoryTestAccess::setDirty(controller, true);
  photo_editor::desktop::TimelineHistoryTestAccess::setMutationBatchActive(
      controller, false);
  if (!nearly_equal(controller.totalDurationSecondsValue(), 8.0)) {
    QObject::disconnect(duration_connection);
    return check(false,
                 "A batched edit must invalidate duration before later commands read it");
  }
  photo_editor::desktop::TimelineHistoryTestAccess::notifyClipsChanged(controller);
  if (!expect_duration(8.0,
                       "A synchronous clipsChanged observer must read fresh duration")) {
    QObject::disconnect(duration_connection);
    return false;
  }

  QQmlEngine qml_engine;
  qml_engine.rootContext()->setContextProperty(
      QStringLiteral("durationController"), &controller);
  QQmlComponent qml_component(&qml_engine);
  qml_component.setData(
      QByteArrayLiteral("import QtQml 2.15\n"
                        "QtObject { property real observedDuration: "
                        "durationController.totalDurationSeconds }"),
      QUrl(QStringLiteral("inmemory:duration-cache.qml")));
  QElapsedTimer qml_load_timer;
  qml_load_timer.start();
  while (qml_component.status() == QQmlComponent::Loading &&
         qml_load_timer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  if (qml_component.status() != QQmlComponent::Ready) {
    for (const auto& error : qml_component.errors()) {
      std::cerr << "QML duration observer error: "
                << error.toString().toStdString() << '\n';
    }
    QObject::disconnect(duration_connection);
    return check(false, "The QML duration observer should finish loading");
  }
  std::unique_ptr<QObject> qml_observer(qml_component.create());
  if (!check(qml_observer != nullptr,
             "The QML duration observer should compile and instantiate")) {
    for (const auto& error : qml_component.errors()) {
      std::cerr << "QML duration observer error: "
                << error.toString().toStdString() << '\n';
    }
    QObject::disconnect(duration_connection);
    return false;
  }
  if (!nearly_equal(qml_observer->property("observedDuration").toDouble(), 8.0)) {
    QObject::disconnect(duration_connection);
    return check(false, "The QML binding should read the initial duration");
  }
  const auto qml_scan_count_before =
      photo_editor::desktop::TimelineHistoryTestAccess::
          totalDurationCacheScanCount(controller);
  if (!photo_editor::desktop::TimelineHistoryTestAccess::setClipStart(
          controller, remaining_clip_id, 9.0)) {
    QObject::disconnect(duration_connection);
    return check(false, "The QML freshness fixture should contain a clip");
  }
  photo_editor::desktop::TimelineHistoryTestAccess::notifyClipsChanged(controller);
  const auto qml_duration = qml_observer->property("observedDuration").toDouble();
  if (!check(nearly_equal(qml_duration, 12.0),
             "The declarative QML duration binding must update synchronously") ||
      !expect_duration(12.0,
                       "The controller observers must read the value already published to QML") ||
      !check(photo_editor::desktop::TimelineHistoryTestAccess::
                     totalDurationCacheScanCount(controller) == qml_scan_count_before + 1,
             "One clipsChanged notification should compute aggregate duration once")) {
    QObject::disconnect(duration_connection);
    return false;
  }
  QObject::disconnect(duration_connection);

  const auto saved_document =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller);
  const auto saved_path = QDir(workspace_path).filePath(
      QStringLiteral("duration-cache-reopen.json"));
  QSaveFile save(saved_path);
  const auto serialized = QJsonDocument(saved_document).toJson(QJsonDocument::Compact);
  if (!save.open(QIODevice::WriteOnly) || save.write(serialized) != serialized.size() ||
      !save.commit()) {
    return check(false, "The duration cache reload fixture should be written");
  }
  TimelineDocumentController reopened;
  auto reopened_observer_duration = -1.0;
  const auto reopened_connection = QObject::connect(
      &reopened,
      &TimelineDocumentController::clipsChanged,
      &reopened,
      [&]() { reopened_observer_duration = reopened.totalDurationSecondsValue(); },
      Qt::DirectConnection);
  if (!reopened.loadTimeline(saved_path) ||
      !nearly_equal(reopened_observer_duration, 12.0) ||
      !nearly_equal(reopened.totalDurationSecondsValue(), 12.0)) {
    QObject::disconnect(reopened_connection);
    return check(false, "Reload must invalidate the duration cache before observers run");
  }
  QObject::disconnect(reopened_connection);

  TimelineDocumentController large;
  photo_editor::desktop::TimelineHistoryTestAccess::installDurationCacheFixture(
      large, 855, 642, 805000);
  const auto initial_duration = large.totalDurationSecondsValue();
  const auto initial_scans = photo_editor::desktop::TimelineHistoryTestAccess::
      totalDurationCacheScanCount(large);
  for (auto index = 0; index < 1000; ++index) {
    if (!nearly_equal(large.totalDurationSecondsValue(), initial_duration)) {
      return check(false, "Large repeated duration reads changed the result");
    }
  }
  if (!check(initial_scans == 1 &&
                 photo_editor::desktop::TimelineHistoryTestAccess::
                         totalDurationCacheScanCount(large) == initial_scans,
             "The 855-track/642-clip, 805k-keyframe fixture should scan once across repeated reads")) {
    return false;
  }

  return check(clips_changed_observer_calls >= 8,
               "The synchronous duration observer should see each edit publication");
}
