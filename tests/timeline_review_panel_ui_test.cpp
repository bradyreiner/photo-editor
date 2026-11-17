#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTest>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>

class ReviewNotesDocument final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList reviewNotes READ reviewNotes NOTIFY markersChanged)
  Q_PROPERTY(bool hasActiveTimeline READ hasActiveTimeline CONSTANT)
  Q_PROPERTY(double totalDurationSeconds READ totalDurationSeconds CONSTANT)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(int addCalls READ addCalls NOTIFY countersChanged)
  Q_PROPERTY(int updateCalls READ updateCalls NOTIFY countersChanged)
  Q_PROPERTY(int resolveCalls READ resolveCalls NOTIFY countersChanged)
  Q_PROPERTY(int seekCalls READ seekCalls NOTIFY countersChanged)
  Q_PROPERTY(double lastSeekSeconds READ lastSeekSeconds NOTIFY countersChanged)
  Q_PROPERTY(QVariantMap lastAddArguments READ lastAddArguments NOTIFY countersChanged)
  Q_PROPERTY(bool failNextAdd READ failNextAdd WRITE setFailNextAdd)
  Q_PROPERTY(bool failNextUpdate READ failNextUpdate WRITE setFailNextUpdate)

 public:
  [[nodiscard]] QVariantList reviewNotes() const { return review_notes_; }
  [[nodiscard]] bool hasActiveTimeline() const { return true; }
  [[nodiscard]] double totalDurationSeconds() const { return 120.0; }
  [[nodiscard]] QString status() const { return status_; }
  [[nodiscard]] int addCalls() const { return add_calls_; }
  [[nodiscard]] int updateCalls() const { return update_calls_; }
  [[nodiscard]] int resolveCalls() const { return resolve_calls_; }
  [[nodiscard]] int seekCalls() const { return seek_calls_; }
  [[nodiscard]] double lastSeekSeconds() const { return last_seek_seconds_; }
  [[nodiscard]] QVariantMap lastAddArguments() const { return last_add_arguments_; }
  [[nodiscard]] bool failNextAdd() const { return fail_next_add_; }
  [[nodiscard]] bool failNextUpdate() const { return fail_next_update_; }

  void setFailNextAdd(bool value) { fail_next_add_ = value; }
  void setFailNextUpdate(bool value) { fail_next_update_ = value; }

  Q_INVOKABLE QString addAnchoredReviewNote(
      const QString& label,
      const QString& note,
      qint64 start_frame,
      qint64 end_frame,
      const QVariantList& clip_ids,
      const QString& assignee,
      const QString& approval_state) {
    ++add_calls_;
    last_add_arguments_ = QVariantMap{
        {QStringLiteral("label"), label},
        {QStringLiteral("note"), note},
        {QStringLiteral("startFrame"), start_frame},
        {QStringLiteral("endFrame"), end_frame},
        {QStringLiteral("clipIds"), clip_ids},
        {QStringLiteral("assignee"), assignee},
        {QStringLiteral("approvalState"), approval_state},
    };
    if (fail_next_add_) {
      fail_next_add_ = false;
      status_ = QStringLiteral("The review note could not be saved.");
      emit countersChanged();
      emit statusChanged();
      return {};
    }

    const auto marker_id = QStringLiteral("note-%1").arg(review_notes_.size() + 1);
    QVariantMap note_row{
        {QStringLiteral("markerId"), marker_id},
        {QStringLiteral("label"), label.isEmpty() ? QStringLiteral("Review note") : label},
        {QStringLiteral("note"), note},
        {QStringLiteral("seconds"), static_cast<double>(start_frame) / 30.0},
        {QStringLiteral("frame"), start_frame},
        {QStringLiteral("endFrame"), end_frame},
        {QStringLiteral("anchorType"), end_frame > start_frame ? QStringLiteral("range") : QStringLiteral("frame")},
        {QStringLiteral("clipIds"), clip_ids},
        {QStringLiteral("assignee"), assignee},
        {QStringLiteral("approvalState"), approval_state},
        {QStringLiteral("resolved"), false},
        {QStringLiteral("completed"), false},
    };
    review_notes_.push_back(note_row);
    status_ = QStringLiteral("Added review note.");
    emit markersChanged();
    emit countersChanged();
    emit statusChanged();
    return marker_id;
  }

  Q_INVOKABLE bool updateAnchoredReviewNote(
      const QString& marker_id,
      const QString& label,
      const QString& note,
      qint64 start_frame,
      qint64 end_frame,
      const QVariantList& clip_ids,
      const QString& assignee,
      const QString& approval_state,
      bool resolved) {
    ++update_calls_;
    emit countersChanged();
    if (fail_next_update_) {
      fail_next_update_ = false;
      status_ = QStringLiteral("The review note could not be updated.");
      emit statusChanged();
      return false;
    }
    for (auto& row_value : review_notes_) {
      auto row = row_value.toMap();
      if (row.value(QStringLiteral("markerId")).toString() != marker_id)
        continue;
      row.insert(QStringLiteral("label"), label.isEmpty() ? QStringLiteral("Review note") : label);
      row.insert(QStringLiteral("note"), note);
      row.insert(QStringLiteral("frame"), start_frame);
      row.insert(QStringLiteral("endFrame"), end_frame);
      row.insert(QStringLiteral("seconds"), static_cast<double>(start_frame) / 30.0);
      row.insert(QStringLiteral("anchorType"), end_frame > start_frame ? QStringLiteral("range") : QStringLiteral("frame"));
      row.insert(QStringLiteral("clipIds"), clip_ids);
      row.insert(QStringLiteral("assignee"), assignee);
      row.insert(QStringLiteral("approvalState"), approval_state);
      row.insert(QStringLiteral("resolved"), resolved);
      row.insert(QStringLiteral("completed"), resolved);
      row_value = row;
      status_ = QStringLiteral("Updated review note.");
      emit markersChanged();
      emit statusChanged();
      return true;
    }
    return false;
  }

  Q_INVOKABLE bool resolveTimelineReviewNote(const QString& marker_id, bool resolved) {
    ++resolve_calls_;
    for (auto& row_value : review_notes_) {
      auto row = row_value.toMap();
      if (row.value(QStringLiteral("markerId")).toString() != marker_id)
        continue;
      if (row.value(QStringLiteral("resolved")).toBool() == resolved)
        return false;
      row.insert(QStringLiteral("resolved"), resolved);
      row.insert(QStringLiteral("completed"), resolved);
      row_value = row;
      status_ = resolved ? QStringLiteral("Resolved review note.")
                         : QStringLiteral("Reopened review note.");
      emit markersChanged();
      emit countersChanged();
      emit statusChanged();
      return true;
    }
    emit countersChanged();
    emit statusChanged();
    return false;
  }

  Q_INVOKABLE bool seekSequenceSeconds(double seconds) {
    ++seek_calls_;
    last_seek_seconds_ = seconds;
    emit countersChanged();
    return true;
  }

 signals:
  void markersChanged();
  void countersChanged();
  void statusChanged();

 private:
  QVariantList review_notes_;
  QVariantMap last_add_arguments_;
  QString status_;
  int add_calls_ = 0;
  int update_calls_ = 0;
  int resolve_calls_ = 0;
  int seek_calls_ = 0;
  double last_seek_seconds_ = -1.0;
  bool fail_next_add_ = false;
  bool fail_next_update_ = false;
};

namespace {
void settle(int rounds = 18) {
  for (int index = 0; index < rounds; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(3);
  }
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QQuickItem* find_item(QQuickItem* parent, const QString& object_name) {
  if (parent == nullptr)
    return nullptr;
  if (parent->objectName() == object_name)
    return parent;
  const auto children = parent->childItems();
  for (auto* child : children) {
    if (auto* found = find_item(child, object_name))
      return found;
  }
  return nullptr;
}

QQuickItem* find_item(QQuickWindow* window, const QString& object_name) {
  return window == nullptr ? nullptr : find_item(window->contentItem(), object_name);
}

bool scroll_item_into_view(QQuickItem* item);

bool click(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr || !item->isVisible() ||
      !item->isEnabled() ||
      item->width() <= 1.0 || item->height() <= 1.0 ||
      !scroll_item_into_view(item))
    return false;
  const auto center = item->mapToScene(QPointF(item->width() * 0.5, item->height() * 0.5));
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    QPoint(qRound(center.x()), qRound(center.y())));
  settle();
  return true;
}

bool set_text(QQuickItem* item, const QString& value) {
  if (item == nullptr || !item->setProperty("text", value))
    return false;
  settle(4);
  return true;
}

bool scroll_to_bottom(QObject* flickable) {
  if (flickable == nullptr)
    return false;
  const double content_height = flickable->property("contentHeight").toDouble();
  const double viewport_height = flickable->property("height").toDouble();
  if (viewport_height <= 0.0 || content_height < viewport_height)
    return false;
  if (!flickable->setProperty("contentY", content_height - viewport_height))
    return false;
  settle();
  return true;
}

bool scroll_item_into_view(QQuickItem* item) {
  if (item == nullptr)
    return false;
  for (auto* viewport = item->parentItem(); viewport != nullptr;
       viewport = viewport->parentItem()) {
    const auto content_x_value = viewport->property("contentX");
    const auto content_y_value = viewport->property("contentY");
    const auto content_width_value = viewport->property("contentWidth");
    const auto content_height_value = viewport->property("contentHeight");
    bool changed = false;
    double next_x = content_x_value.toDouble();
    double next_y = content_y_value.toDouble();
    const auto item_origin = item->mapToItem(viewport, QPointF(0, 0));

    if (content_x_value.isValid() && content_width_value.isValid() &&
        viewport->width() > 0.0) {
      const double max_x = std::max(0.0, content_width_value.toDouble() - viewport->width());
      if (item->width() <= viewport->width()) {
        if (item_origin.x() < 0.0)
          next_x += item_origin.x();
        else if (item_origin.x() + item->width() > viewport->width())
          next_x += item_origin.x() + item->width() - viewport->width();
      } else if (item_origin.x() + item->width() * 0.5 < 0.0) {
        next_x += item_origin.x() + item->width() * 0.5;
      } else if (item_origin.x() + item->width() * 0.5 > viewport->width()) {
        next_x += item_origin.x() + item->width() * 0.5 - viewport->width();
      }
      next_x = std::clamp(next_x, 0.0, max_x);
      changed = changed || qAbs(next_x - content_x_value.toDouble()) > 0.5;
    }

    if (content_y_value.isValid() && content_height_value.isValid() &&
        viewport->height() > 0.0) {
      const double max_y = std::max(0.0, content_height_value.toDouble() - viewport->height());
      if (item->height() <= viewport->height()) {
        if (item_origin.y() < 0.0)
          next_y += item_origin.y();
        else if (item_origin.y() + item->height() > viewport->height())
          next_y += item_origin.y() + item->height() - viewport->height();
      } else if (item_origin.y() + item->height() * 0.5 < 0.0) {
        next_y += item_origin.y() + item->height() * 0.5;
      } else if (item_origin.y() + item->height() * 0.5 > viewport->height()) {
        next_y += item_origin.y() + item->height() * 0.5 - viewport->height();
      }
      next_y = std::clamp(next_y, 0.0, max_y);
      changed = changed || qAbs(next_y - content_y_value.toDouble()) > 0.5;
    }

    if (!changed)
      continue;
    if (content_x_value.isValid() && !viewport->setProperty("contentX", next_x))
      return false;
    if (content_y_value.isValid() && !viewport->setProperty("contentY", next_y))
      return false;
    if (changed)
      settle(4);

    const auto item_center = item->mapToItem(
        viewport, QPointF(item->width() * 0.5, item->height() * 0.5));
    if ((content_x_value.isValid() &&
         (item_center.x() < -0.5 || item_center.x() > viewport->width() + 0.5)) ||
        (content_y_value.isValid() &&
         (item_center.y() < -0.5 || item_center.y() > viewport->height() + 0.5)))
      return false;
  }
  return true;
}

bool intersects_in_item(QQuickItem* left, QQuickItem* right, QQuickItem* parent) {
  if (left == nullptr || right == nullptr || parent == nullptr)
    return true;
  const auto left_pos = left->mapToItem(parent, QPointF(0, 0));
  const auto right_pos = right->mapToItem(parent, QPointF(0, 0));
  const QRectF left_rect(left_pos, QSizeF(left->width(), left->height()));
  const QRectF right_rect(right_pos, QSizeF(right->width(), right->height()));
  return left_rect.intersects(right_rect);
}

bool contained_in_item(QQuickItem* item, QQuickItem* parent) {
  if (item == nullptr || parent == nullptr)
    return false;
  const auto pos = item->mapToItem(parent, QPointF(0, 0));
  return pos.x() >= -1.0 && pos.y() >= -1.0 &&
         pos.x() + item->width() <= parent->width() + 1.0 &&
         pos.y() + item->height() <= parent->height() + 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  ReviewNotesDocument document;
  engine.rootContext()->setContextProperty(QStringLiteral("testReviewDocument"), &document);
  QQmlComponent component(
      &engine,
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                         "/tests/timeline_review_panel_ui_test.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("TimelineReviewPanel fixture should instantiate with the marker API stub");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* panel = find_item(window, QStringLiteral("testedTimelineReviewPanel"));
  if (window == nullptr || panel == nullptr)
    return fail("The review panel should create a visible QQuickWindow surface");
  window->show();
  settle(40);

  auto* composer = find_item(window, QStringLiteral("timelineReviewNoteComposer"));
  auto* add_button = find_item(window, QStringLiteral("timelineReviewAddNoteButton"));
  auto* clip_context = find_item(window, QStringLiteral("timelineReviewIncludeClipContext"));
  auto* status_message = find_item(window, QStringLiteral("timelineReviewPanelStatus"));
  if (composer == nullptr || add_button == nullptr || clip_context == nullptr ||
      status_message == nullptr ||
      !composer->isEnabled())
    return fail("The frame note composer should be available for the active timeline");

  document.setFailNextAdd(true);
  if (!set_text(composer, QStringLiteral("Keep this draft on write failure")) ||
      !add_button->isEnabled() || !click(window, add_button) || document.reviewNotes().size() != 0 ||
      composer->property("text").toString() != QStringLiteral("Keep this draft on write failure") ||
      !status_message->isVisible() ||
      !status_message->property("text").toString().contains(QStringLiteral("could not be saved")))
    return fail("A rejected note save should leave the user's draft intact");

  if (!set_text(composer, QStringLiteral("Check the shadow edge")) ||
      !click(window, clip_context) || !clip_context->property("checked").toBool() ||
      !add_button->isEnabled() ||
      !click(window, add_button))
    return fail("The point note and optional clip context should submit through the panel");
  const auto point_arguments = document.lastAddArguments();
  const auto point_ids = point_arguments.value(QStringLiteral("clipIds")).toList();
  if (document.reviewNotes().size() != 1 ||
      point_arguments.value(QStringLiteral("startFrame")).toLongLong() != 90 ||
      point_arguments.value(QStringLiteral("endFrame")).toLongLong() != 90 ||
      point_ids.size() != 1 || point_ids.first().toString() != QStringLiteral("clip-17") ||
      composer->property("text").toString().length() != 0)
    return fail("A point note should use the current frame and persist the optional clip ID");

  auto* range_anchor = find_item(window, QStringLiteral("timelineReviewAnchorRange"));
  if (!click(window, range_anchor))
    return fail("The range anchor control should be reachable in the narrow panel");
  settle();
  auto* range_start = find_item(window, QStringLiteral("timelineReviewRangeStart"));
  auto* range_end = find_item(window, QStringLiteral("timelineReviewRangeEnd"));
  auto* load_in_out = find_item(window, QStringLiteral("timelineReviewLoadInOutRange"));
  if (range_start == nullptr || range_end == nullptr ||
      range_start->width() <= 20 || range_end->width() <= 20 ||
      !contained_in_item(range_start, panel) || !contained_in_item(range_end, panel) ||
      intersects_in_item(range_start, range_end, panel) ||
      load_in_out == nullptr || !load_in_out->isVisible() ||
      !contained_in_item(load_in_out, panel) ||
      intersects_in_item(range_start, load_in_out, panel) ||
      intersects_in_item(range_end, load_in_out, panel))
    return fail("In/Out fields should remain visible and separated at narrow width");
  if (!set_text(range_start, QStringLiteral("4.000")) ||
      !set_text(range_end, QStringLiteral("5.000")) ||
      !click(window, load_in_out) ||
      range_start->property("text").toString() != QStringLiteral("1.000") ||
      range_end->property("text").toString() != QStringLiteral("2.000"))
    return fail("Load In-Out should copy the current sequence range into the composer");
  if (!set_text(composer, QStringLiteral("Review the transition")) ||
      !add_button->isEnabled())
    return fail("A complete range and note body should enable submission");
  if (!set_text(range_end, QString()) || add_button->isEnabled() ||
      !set_text(range_end, QStringLiteral("2.000")) || !add_button->isEnabled())
    return fail("An empty range bound should block submission until corrected");
  if (!click(window, add_button))
    return fail("The range note should submit from the same composer");
  const auto range_arguments = document.lastAddArguments();
  if (document.reviewNotes().size() != 2 ||
      range_arguments.value(QStringLiteral("startFrame")).toLongLong() != 30 ||
      range_arguments.value(QStringLiteral("endFrame")).toLongLong() != 60 ||
      range_arguments.value(QStringLiteral("clipIds")).toList().size() != 0 ||
      !contained_in_item(add_button, panel))
    return fail("The In/Out note should use half-open frame bounds without inherited clip context");

  auto* all_filter = find_item(window, QStringLiteral("timelineReviewFilter_all"));
  if (!click(window, all_filter))
    return fail("The all-notes filter should be clickable");
  auto* note_list = find_item(window, QStringLiteral("timelineReviewPanelList"));
  if (note_list == nullptr || !scroll_to_bottom(note_list))
    return fail("The bounded review list should scroll to its lower note rows");
  auto* edit_button = find_item(window, QStringLiteral("timelineReviewNoteEditButton-note-2"));
  if (!click(window, edit_button))
    return fail("Each note should expose an inline edit action");
  auto* edit_body = find_item(window, QStringLiteral("timelineReviewNoteEditBody"));
  auto* edit_end = find_item(window, QStringLiteral("timelineReviewNoteEditEnd"));
  auto* edit_save = find_item(window, QStringLiteral("timelineReviewNoteSaveButton-note-2"));
  if (edit_body == nullptr || edit_end == nullptr || edit_save == nullptr ||
      !set_text(edit_body, QStringLiteral("Transition needs more hold time")) ||
      !set_text(edit_end, QStringLiteral("2.500")) ||
      !scroll_to_bottom(note_list) || !contained_in_item(edit_save, note_list))
    return fail("The range note should save edited text and In/Out values");
  document.setFailNextUpdate(true);
  if (!click(window, edit_save) ||
      panel->property("editingMarkerId").toString() != QStringLiteral("note-2") ||
      edit_body->property("text").toString() != QStringLiteral("Transition needs more hold time") ||
      document.reviewNotes().at(1).toMap().value(QStringLiteral("endFrame")).toLongLong() != 60)
    return fail("A rejected edit should preserve the editing draft and original note");
  if (!click(window, edit_save))
    return fail("The edited range note should remain saveable after a failed attempt");
  const auto updated_range = document.reviewNotes().at(1).toMap();
  if (document.updateCalls() != 2 ||
      updated_range.value(QStringLiteral("note")).toString() !=
          QStringLiteral("Transition needs more hold time") ||
      updated_range.value(QStringLiteral("endFrame")).toLongLong() != 75)
    return fail("Saving an edit should retain the existing Marker and update its range");

  if (!set_text(composer, QStringLiteral("Keep this while jumping")))
    return fail("The composer should accept a new draft while browsing notes");
  auto* jump_button = find_item(window, QStringLiteral("timelineReviewNoteJumpButton-note-2"));
  if (!click(window, jump_button) || document.seekCalls() != 1 ||
      qAbs(document.lastSeekSeconds() - 1.0) > 0.001 ||
      composer->property("text").toString() != QStringLiteral("Keep this while jumping"))
    return fail("Jumping to a note should seek to its anchor and preserve the current draft");

  auto* resolve_button = find_item(window, QStringLiteral("timelineReviewNoteResolveButton-note-1"));
  if (!click(window, resolve_button) || document.resolveCalls() != 1 ||
      !document.reviewNotes().first().toMap().value(QStringLiteral("resolved")).toBool())
    return fail("A note should resolve through the existing review marker API");
  auto* open_filter = find_item(window, QStringLiteral("timelineReviewFilter_open"));
  auto* resolved_filter = find_item(window, QStringLiteral("timelineReviewFilter_resolved"));
  if (!click(window, open_filter) || panel->property("visibleNotes").toList().size() != 1)
    return fail("The Open filter should hide resolved notes");
  if (!click(window, resolved_filter) || panel->property("visibleNotes").toList().size() != 1)
    return fail("The Resolved filter should show only resolved notes");
  auto* reopen_button = find_item(window, QStringLiteral("timelineReviewNoteResolveButton-note-1"));
  if (!click(window, reopen_button) ||
      document.reviewNotes().first().toMap().value(QStringLiteral("resolved")).toBool())
    return fail("A resolved note should reopen through the same marker API");
  if (!click(window, open_filter) || panel->property("visibleNotes").toList().size() != 2)
    return fail("Reopened notes should return to the Open filter");

  auto* unchanged_edit_button = find_item(window, QStringLiteral("timelineReviewNoteEditButton-note-1"));
  if (!click(window, unchanged_edit_button))
    return fail("A note should remain editable after reopening");
  auto* unchanged_save = find_item(window, QStringLiteral("timelineReviewNoteSaveButton-note-1"));
  if (!click(window, unchanged_save) || document.updateCalls() != 2 ||
      !panel->property("editingMarkerId").toString().isEmpty())
    return fail("Saving an unchanged note should close the editor without a false write failure");

  auto* scroll_view = find_item(window, QStringLiteral("timelineReviewPanelScrollView"));
  auto* composer_section = find_item(window, QStringLiteral("timelineReviewComposerSection"));
  QObject* scroll_content = scroll_view == nullptr
                                ? nullptr
                                : scroll_view->property("contentItem").value<QObject*>();
  if (scroll_content == nullptr || note_list == nullptr || composer_section == nullptr)
    return fail("The compact review panel should expose its bounded scrolling content");
  window->resize(360, 300);
  settle(40);
  const double content_height = scroll_content->property("contentHeight").toDouble();
  const double viewport_height = scroll_content->property("height").toDouble();
  if (panel->height() > 301 || viewport_height <= 0.0 ||
      content_height <= viewport_height || note_list->height() > 100.0 ||
      panel->property("radius").toDouble() != 0.0)
    return fail("A 300px dock should use bounded list height and scrollable, square-edged content");
  if (composer->property("text").toString() != QStringLiteral("Keep this while jumping"))
    return fail("Resizing the review panel should preserve the current composer draft");
  if (!scroll_content->setProperty("contentY", 0.0))
    return fail("The short review panel should scroll to its header");
  settle();
  if (!scroll_content->setProperty("contentY", content_height - viewport_height))
    return fail("The short review panel should scroll to the composer");
  settle();
  if (!contained_in_item(add_button, panel) || !composer->isVisible() ||
      composer->property("text").toString() != QStringLiteral("Keep this while jumping"))
    return fail("The Add action should be reachable at 300px without losing the draft");
  const int note_count_before_short_dock_submit = document.reviewNotes().size();
  if (!click(window, add_button) ||
      document.reviewNotes().size() != note_count_before_short_dock_submit + 1)
    return fail("A note should submit from the bottom of the short dock scroll area");

  if (find_item(window, QStringLiteral("timelineReviewExecuteButton")) != nullptr)
    return fail("Review notes should not expose agent execution controls");
  return EXIT_SUCCESS;
}

#include "timeline_review_panel_ui_test.moc"
