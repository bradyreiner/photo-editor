#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJSValue>
#include <QKeyEvent>
#include <QLineF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <iostream>
#include <memory>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

namespace {

void settle() {
  for (int i = 0; i < 6; ++i)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

int fail(const char* message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

QQuickItem* find_item(QQuickItem* parent, const QString& name) {
  if (!parent) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems())
    if (auto* found = find_item(child, name)) return found;
  return nullptr;
}

QVariant unpack(QVariant value) {
  if (value.metaType() == QMetaType::fromType<QJSValue>())
    return value.value<QJSValue>().toVariant();
  return value;
}

QVariantMap current_document(QObject* overlay) {
  return unpack(overlay->property("editDocument")).toMap();
}

QVariantList subpath_anchors(QObject* overlay) {
  const auto document = current_document(overlay);
  const auto subpaths = document.value(QStringLiteral("subpaths")).toList();
  return subpaths.isEmpty() ? QVariantList{} : subpaths.first().toMap()
      .value(QStringLiteral("anchors")).toList();
}

qsizetype total_anchor_count(QObject* overlay) {
  const auto document = current_document(overlay);
  qsizetype total = 0;
  for (const auto& subpath : document.value(QStringLiteral("subpaths")).toList())
    total += subpath.toMap().value(QStringLiteral("anchors")).toList().size();
  return total;
}

QPointF path_point_to_view(const QVariant& anchor_value, int width, int height) {
  const auto anchor = anchor_value.toMap();
  const auto point = anchor.value(QStringLiteral("point")).toList();
  if (point.size() < 2) return {};
  return QPointF(point.at(0).toDouble() * width, point.at(1).toDouble() * height);
}

QPointF path_point(const QVariantMap& anchor, const QString& key) {
  const auto value = anchor.value(key).toList();
  if (value.size() < 2) return {};
  return QPointF(value.at(0).toDouble(), value.at(1).toDouble());
}

QPointF cubic_segment_point(const QVariantMap& subpath, qsizetype segment, double t) {
  const auto anchors = subpath.value(QStringLiteral("anchors")).toList();
  if (anchors.size() < 2 || segment < 0 || segment >= anchors.size()) return {};
  const qsizetype end_index = (segment + 1) % anchors.size();
  const auto start = anchors.at(segment).toMap();
  const auto end = anchors.at(end_index).toMap();
  const auto p0 = path_point(start, QStringLiteral("point"));
  const auto p3 = path_point(end, QStringLiteral("point"));
  const auto p1 = p0 + path_point(start, QStringLiteral("out"));
  const auto p2 = p3 + path_point(end, QStringLiteral("in"));
  const double u = 1.0 - t;
  return p0 * (u * u * u) + p1 * (3.0 * u * u * t) +
         p2 * (3.0 * u * t * t) + p3 * (t * t * t);
}

QVariantMap first_subpath(QObject* overlay) {
  const auto subpaths = current_document(overlay).value(QStringLiteral("subpaths")).toList();
  return subpaths.isEmpty() ? QVariantMap{} : subpaths.first().toMap();
}

QVariantMap selected_anchor(QObject* overlay) {
  const auto document = current_document(overlay);
  const auto selected_subpath = overlay->property("selectedSubpathId").toString();
  const auto selected_id = overlay->property("selectedAnchorId").toString();
  for (const auto& subpath_value : document.value(QStringLiteral("subpaths")).toList()) {
    const auto subpath = subpath_value.toMap();
    if (subpath.value(QStringLiteral("id")).toString() != selected_subpath) continue;
    for (const auto& anchor_value : subpath.value(QStringLiteral("anchors")).toList()) {
      const auto anchor = anchor_value.toMap();
      if (anchor.value(QStringLiteral("id")).toString() == selected_id) return anchor;
    }
  }
  return {};
}

QByteArray json_document(QObject* overlay) {
  return QJsonDocument::fromVariant(current_document(overlay)).toJson(QJsonDocument::Compact);
}

QByteArray json_value(QVariant value) {
  return QJsonDocument::fromVariant(unpack(value)).toJson(QJsonDocument::Compact);
}

void pointer(QQuickWindow& window, QEvent::Type type, QPointF point,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
  const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
  QMouseEvent event(type, point, window.mapToGlobal(point.toPoint()), button, buttons, modifiers);
  QCoreApplication::sendEvent(&window, &event);
  settle();
}

void click(QQuickWindow& window, QPointF point,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  pointer(window, QEvent::MouseButtonPress, point, modifiers);
  pointer(window, QEvent::MouseButtonRelease, point, modifiers);
}

void drag(QQuickWindow& window, QPointF start, QPointF finish,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  pointer(window, QEvent::MouseButtonPress, start, modifiers);
  for (int step = 1; step <= 6; ++step)
    pointer(window, QEvent::MouseMove, start + (finish - start) * (step / 6.0), modifiers);
  pointer(window, QEvent::MouseButtonRelease, finish, modifiers);
}

void key(QQuickWindow& window, Qt::Key key_value) {
  window.requestActivate();
  settle();
  QTest::keyClick(&window, key_value);
  settle();
}

void type_numeric(QQuickWindow& window, const QString& value) {
  for (const QChar character : value) {
    const auto key_value = character == QLatin1Char('.')
        ? Qt::Key_Period
        : static_cast<Qt::Key>(Qt::Key_0 + character.digitValue());
    QTest::keyClick(&window, key_value);
    settle();
  }
}

bool click_item(QQuickWindow& window, QQuickItem* item) {
  if (!item || !item->isVisible() || !item->isEnabled()) return false;
  const auto point = item->mapToScene(item->boundingRect().center());
  click(window, point);
  return true;
}

QColor window_pixel(QQuickWindow& window, QPoint point) {
  const auto image = window.grabWindow();
  return image.isNull() || !image.rect().contains(point) ? QColor{} : image.pixelColor(point);
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError>& errors) {
    for (const auto& warning : errors)
      std::cerr << warning.toString().toStdString() << '\n';
  });
  const QString component_root = QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/");
  QQmlComponent overlay_component(&engine, QUrl::fromLocalFile(component_root + QStringLiteral("PhotoPenOverlay.qml")));
  if (overlay_component.isError()) {
    std::cerr << overlay_component.errorString().toStdString() << '\n';
    return fail("Load PhotoPenOverlay.qml from the repository source");
  }
  const QVariantMap initial_overlay_properties{
      {QStringLiteral("active"), true},
      {QStringLiteral("canvasWidth"), 200},
      {QStringLiteral("canvasHeight"), 200},
      {QStringLiteral("imageWidth"), 200.0},
      {QStringLiteral("imageHeight"), 200.0}};
  std::unique_ptr<QObject> overlay_object(
      overlay_component.createWithInitialProperties(initial_overlay_properties));
  auto* overlay = qobject_cast<QQuickItem*>(overlay_object.get());
  if (!overlay) {
    std::cerr << overlay_component.errorString().toStdString() << '\n';
    return fail("Create the Pen overlay with document and view geometry");
  }

  QQuickWindow window;
  window.resize(200, 200);
  window.setColor(QColor(QStringLiteral("#272f3d")));
  overlay->setParentItem(window.contentItem());
  overlay->setSize(QSizeF(200, 200));
  window.show();
  window.requestActivate();
  settle();

  QQmlComponent controls_component(
      &engine, QUrl::fromLocalFile(component_root + QStringLiteral("PhotoVectorPathControls.qml")));
  if (controls_component.isError()) {
    std::cerr << controls_component.errorString().toStdString() << '\n';
    return fail("Load PhotoVectorPathControls.qml from the repository source");
  }
  QQuickWindow controls_window;
  controls_window.resize(760, 340);
  controls_window.setColor(QColor(QStringLiteral("#141a22")));
  const QVariantMap controls_properties{{QStringLiteral("overlay"), QVariant::fromValue(overlay)}};
  std::unique_ptr<QObject> controls_object(
      controls_component.createWithInitialProperties(controls_properties));
  auto* controls = qobject_cast<QQuickItem*>(controls_object.get());
  if (!controls) {
    std::cerr << controls_component.errorString().toStdString() << '\n';
    return fail("Create path controls connected to the live overlay");
  }
  controls->setParentItem(controls_window.contentItem());
  controls->setSize(QSizeF(760, 220));
  controls_window.show();
  controls_window.requestActivate();
  settle();

  auto* close_button = find_item(controls, QStringLiteral("photoVectorPathCloseButton"));
  auto* finish_button = find_item(controls, QStringLiteral("photoVectorPathFinishButton"));
  auto* cancel_button = find_item(controls, QStringLiteral("photoVectorPathCancelButton"));
  auto* layer_target = find_item(controls, QStringLiteral("photoVectorPathLayerTargetButton"));
  auto* selection_target = find_item(controls, QStringLiteral("photoVectorPathSelectionTargetButton"));
  auto* fill_field = find_item(controls, QStringLiteral("photoVectorPathFillColorField"));
  auto* stroke_field = find_item(controls, QStringLiteral("photoVectorPathStrokeColorField"));
  auto* width_input = find_item(controls, QStringLiteral("photoVectorPathStrokeWidthField"));
  auto* instructions = find_item(controls, QStringLiteral("photoVectorPathInstructions"));
  auto* selection_guidance = find_item(controls, QStringLiteral("photoVectorPathSelectionGuidance"));
  auto* delete_anchor_button = find_item(controls, QStringLiteral("photoVectorPathDeleteAnchorButton"));
  auto* corner_button = find_item(controls, QStringLiteral("photoVectorPathCornerButton"));
  auto* smooth_button = find_item(controls, QStringLiteral("photoVectorPathSmoothButton"));
  auto* handle_link_button = find_item(controls, QStringLiteral("photoVectorPathHandleLinkButton"));
  auto* anchor_x_field = find_item(controls, QStringLiteral("photoVectorPathAnchorXField"));
  auto* anchor_y_field = find_item(controls, QStringLiteral("photoVectorPathAnchorYField"));
  auto* commit_error = find_item(controls, QStringLiteral("photoVectorPathCommitError"));
  if (!close_button || !finish_button || !cancel_button || !fill_field || !stroke_field ||
      !width_input || !instructions || !layer_target || !selection_target || !selection_guidance ||
      !delete_anchor_button || !corner_button || !smooth_button || !handle_link_button ||
      !anchor_x_field || !anchor_y_field || !commit_error)
    return fail("Expose controls for path commands, selected anchors, appearance and instructions");
  if (!instructions->property("text").toString().contains(QStringLiteral("Alt-drag")))
    return fail("Explain Alt-drag handle editing in the compact controls");
  if (!click_item(controls_window, selection_target) ||
      controls->property("outputMode").toString() != QStringLiteral("selection") ||
      !click_item(controls_window, layer_target) ||
      controls->property("outputMode").toString() != QStringLiteral("layer"))
    return fail("Choose Shape layer or Selection as the path destination");

  QSignalSpy previews(overlay, SIGNAL(previewChanged(QVariant)));
  QSignalSpy commits(overlay, SIGNAL(commitRequested(QVariant)));
  QSignalSpy cancels(overlay, SIGNAL(cancelRequested(QVariant)));
  QSignalSpy no_op_finishes(overlay, SIGNAL(finishedWithoutChanges()));
  QSignalSpy mapping_errors(overlay, SIGNAL(mappingFailed(QString)));
  QSignalSpy appearance(controls, SIGNAL(appearanceChanged(QString,QString,double)));
  if (!previews.isValid() || !commits.isValid() || !cancels.isValid() || !no_op_finishes.isValid() ||
      !mapping_errors.isValid() || !appearance.isValid())
    return fail("Observe overlay transactions and path appearance signals");

  // Draw the first anchor by click-drag, then add two more with clicks.
  drag(window, QPointF(30, 160), QPointF(50, 140));
  click(window, QPointF(160, 40));
  click(window, QPointF(140, 150));
  if (subpath_anchors(overlay).size() != 3 || previews.size() < 3)
    return fail("Use real pointer click and drag events to create an editable cubic path");
  if (overlay->property("hasClosedPath").toBool())
    return fail("An open path must not be reported as a closed selection contour");
  if (!click_item(controls_window, selection_target) || finish_button->isEnabled() ||
      !selection_guidance->isVisible() ||
      !selection_guidance->property("text").toString().contains(QStringLiteral("Close a path")))
    return fail("Require a closed contour before finishing a path as a selection");
  if (!QMetaObject::invokeMethod(overlay, "finishPath") || commits.size() != 0 ||
      !overlay->property("transactionOpen").toBool())
    return fail("Keep Enter and Finish from committing an open selection contour");
  const auto background = window.color();
  const auto open_interior = window_pixel(window, QPoint(100, 120));
  if (!open_interior.isValid() || open_interior != background)
    return fail("Keep the interior of an open contour unfilled in the live overlay");
  const bool close_clicked = click_item(controls_window, close_button);
  if (!close_clicked || !overlay->property("transactionOpen").toBool()) {
    return fail("Close the open three-anchor path from the controls panel");
  }
  if (!overlay->property("hasClosedPath").toBool() || !finish_button->isEnabled())
    return fail("Enable selection finishing after a contour is explicitly closed");
  const auto closed_interior = window_pixel(window, QPoint(100, 120));
  if (!closed_interior.isValid() || closed_interior == background)
    return fail("Show a fill preview after closing the contour");
  if (argc > 1) {
    const auto evidence_directory = QString::fromLocal8Bit(argv[1]);
    if (!QDir().mkpath(evidence_directory) ||
        !window.grabWindow().save(evidence_directory + QStringLiteral("/photo-pen-overlay-ui.png")) ||
        !controls_window.grabWindow().save(evidence_directory + QStringLiteral("/photo-pen-controls-ui.png")))
      return fail("Save optional closed-path Pen and controls UI captures");
  }
  if (!click_item(controls_window, layer_target))
    return fail("Restore the Shape layer destination before committing the UI fixture");
  if (!click_item(controls_window, finish_button) || commits.size() != 1)
    return fail("Finish the whole creation gesture with exactly one commit");
  if (commits.size() != 1)
    return fail("A click transaction emits only one commit signal");
  const auto creation_doc = current_document(overlay);
  const auto created_subpath = creation_doc.value(QStringLiteral("subpaths")).toList().first().toMap();
  if (!created_subpath.value(QStringLiteral("closed")).toBool())
    return fail("Closed state is represented in the committed path recipe");

  // Move the first anchor using its hit target, then commit with Enter.
  const auto initial_anchors = subpath_anchors(overlay);
  const QPointF first_anchor = path_point_to_view(initial_anchors.first(), 200, 200);
  drag(window, first_anchor, first_anchor + QPointF(10, -10));
  key(window, Qt::Key_Return);
  if (commits.size() != 2)
    return fail("Anchor edits commit once when Enter is pressed");
  const auto moved_anchors = subpath_anchors(overlay);
  const QPointF moved_first = path_point_to_view(moved_anchors.first(), 200, 200);
  if (QLineF(moved_first, first_anchor + QPointF(10, -10)).length() > 1.5)
    return fail("Pointer dragging moves the selected stable anchor");

  // The selected anchor keeps its handles. Alt-drag its outgoing handle and
  // verify the edit is reflected in the path rather than only in the preview.
  const auto moved_first_anchor = moved_anchors.first().toMap();
  const auto outgoing = moved_first_anchor.value(QStringLiteral("out")).toList();
  if (outgoing.size() < 2)
    return fail("Smooth pointer-created anchors retain outgoing handle coordinates");
  const QPointF handle_start(moved_first.x() + outgoing.at(0).toDouble() * 200,
                             moved_first.y() + outgoing.at(1).toDouble() * 200);
  const QPointF handle_end = handle_start + QPointF(8, -4);
  drag(window, handle_start, handle_end, Qt::AltModifier);
  key(window, Qt::Key_Enter);
  if (commits.size() != 3)
    return fail("Handle edits commit once when Enter is pressed");
  const auto edited_anchor = subpath_anchors(overlay).first().toMap();
  if (edited_anchor.value(QStringLiteral("handlesLinked")).toBool())
    return fail("Alt-drag breaks linked Bezier handles");

  // A new click can be cancelled without leaking the previewed anchor.
  const auto count_before_cancel = total_anchor_count(overlay);
  const auto document_before_cancel = json_document(overlay);
  click(window, QPointF(100, 100));
  if (total_anchor_count(overlay) != count_before_cancel + 1)
    return fail("Preview an added anchor before cancellation");
  key(window, Qt::Key_Escape);
  if (cancels.size() != 1 || commits.size() != 3 ||
      total_anchor_count(overlay) != count_before_cancel ||
      json_document(overlay) != document_before_cancel)
    return fail("Escape cancels the whole pending edit and restores the exact document snapshot");

  // Parent transform errors are explicit and never create a point at the origin.
  const auto count_before_mapping_failure = total_anchor_count(overlay);
  overlay->setProperty("mapViewToPath", QStringLiteral("not-a-function"));
  click(window, QPointF(90, 90));
  if (mapping_errors.size() != 1 ||
      total_anchor_count(overlay) != count_before_mapping_failure)
    return fail("Invalid custom coordinate mapping is surfaced without editing at zero");
  overlay->setProperty("mapViewToPath", QVariant());

  const QJSValue view_mapper = engine.evaluate(
      QStringLiteral("(function(point) { return ({x: 20 + point.x * 160, y: 20 + point.y * 160}); })"));
  const QJSValue path_mapper = engine.evaluate(
      QStringLiteral("(function(point) { return ({x: (point.x - 20) / 160, y: (point.y - 20) / 160}); })"));
  if (!view_mapper.isCallable() || !path_mapper.isCallable())
    return fail("Create JavaScript view/path mapping callbacks");
  overlay->setProperty("mapPathToView", QVariant::fromValue(view_mapper));
  overlay->setProperty("mapViewToPath", QVariant::fromValue(path_mapper));
  const auto before_mapped_anchor = subpath_anchors(overlay).first().toMap()
      .value(QStringLiteral("point")).toList();
  const QPointF mapped_start(20 + before_mapped_anchor.at(0).toDouble() * 160,
                             20 + before_mapped_anchor.at(1).toDouble() * 160);
  drag(window, mapped_start, mapped_start + QPointF(8, -8));
  key(window, Qt::Key_Return);
  const auto after_mapped_anchor = subpath_anchors(overlay).first().toMap()
      .value(QStringLiteral("point")).toList();
  if (commits.size() != 4 ||
      std::abs(after_mapped_anchor.at(0).toDouble() - before_mapped_anchor.at(0).toDouble() - 0.05) > 0.01 ||
      std::abs(after_mapped_anchor.at(1).toDouble() - before_mapped_anchor.at(1).toDouble() + 0.05) > 0.01)
    return fail("Apply both optional transform hooks to anchor hit-testing and edits");
  overlay->setProperty("mapPathToView", QVariant());
  overlay->setProperty("mapViewToPath", QVariant());

  const auto selection_anchors = subpath_anchors(overlay);
  const QPointF selection_point = path_point_to_view(selection_anchors.first(), 200, 200);
  click(window, selection_point);
  if (!overlay->property("transactionOpen").toBool() ||
      overlay->property("selectedAnchorId").toString() !=
          selection_anchors.first().toMap().value(QStringLiteral("id")).toString())
    return fail("Pointer selection gives the chosen anchor an explicit active selection");
  key(window, Qt::Key_Return);
  if (commits.size() != 4 || no_op_finishes.size() != 1 ||
      overlay->property("transactionOpen").toBool())
    return fail("Finish a selection-only click without creating a document mutation");

  // Insert a point directly on a curved segment. Its cubic subdivision must
  // preserve the original geometry on both sides of the inserted anchor.
  const auto path_before_insert = first_subpath(overlay);
  const auto anchors_before_insert = path_before_insert.value(QStringLiteral("anchors")).toList();
  const QPointF segment_midpoint = cubic_segment_point(path_before_insert, 0, 0.5) * 200.0;
  click(window, segment_midpoint);
  const auto path_after_insert = first_subpath(overlay);
  const auto anchors_after_insert = path_after_insert.value(QStringLiteral("anchors")).toList();
  if (anchors_after_insert.size() != anchors_before_insert.size() + 1 ||
      !overlay->property("hasSelectedAnchor").toBool())
    return fail("Clicking a path segment inserts and selects an anchor");
  for (double t : {0.1, 0.25, 0.4, 0.6, 0.75, 0.9}) {
    const auto before = cubic_segment_point(path_before_insert, 0, t);
    const auto after = t <= 0.5
        ? cubic_segment_point(path_after_insert, 0, t * 2.0)
        : cubic_segment_point(path_after_insert, 1, (t - 0.5) * 2.0);
    if (QLineF(before, after).length() > 0.001)
      return fail("De Casteljau segment subdivision preserves the existing curve");
  }
  const auto inserted_anchor_id = overlay->property("selectedAnchorId").toString();
  if (!click_item(controls_window, corner_button) ||
      selected_anchor(overlay).value(QStringLiteral("kind")).toString() != QStringLiteral("corner") ||
      selected_anchor(overlay).value(QStringLiteral("handlesLinked")).toBool())
    return fail("Convert the selected segment anchor to an unlinked corner");
  if (!click_item(controls_window, smooth_button) ||
      selected_anchor(overlay).value(QStringLiteral("kind")).toString() != QStringLiteral("smooth") ||
      !selected_anchor(overlay).value(QStringLiteral("handlesLinked")).toBool())
    return fail("Convert the selected anchor to a smooth linked tangent");
  if (!click_item(controls_window, handle_link_button) ||
      selected_anchor(overlay).value(QStringLiteral("handlesLinked")).toBool() ||
      !click_item(controls_window, handle_link_button) ||
      !selected_anchor(overlay).value(QStringLiteral("handlesLinked")).toBool())
    return fail("Break and relink the selected anchor handles from the controls");
  if (!click_item(controls_window, finish_button) || commits.size() != 5)
    return fail("Commit segment insertion and tangent changes as one path operation");

  // Delete must remove only the selected anchor and preserve its containing path.
  const auto before_delete = total_anchor_count(overlay);
  key(window, Qt::Key_Delete);
  if (total_anchor_count(overlay) != before_delete - 1 ||
      current_document(overlay).value(QStringLiteral("subpaths")).toList().isEmpty() ||
      overlay->property("selectedAnchorId").toString() == inserted_anchor_id)
    return fail("Delete removes the selected anchor while preserving and selecting its path");
  key(window, Qt::Key_Return);
  if (commits.size() != 6)
    return fail("Commit selected-anchor deletion with Enter");

  // Numeric fields edit the selected point in normalized image percentages.
  controls_window.requestActivate();
  settle();
  if (!click_item(controls_window, anchor_x_field) ||
      !QMetaObject::invokeMethod(anchor_x_field, "selectAll"))
    return fail("Focus the selected-anchor X position field");
  const auto numeric_y_before = selected_anchor(overlay).value(QStringLiteral("point")).toList().at(1).toDouble();
  type_numeric(controls_window, QStringLiteral("82.50"));
  QTest::keyClick(&controls_window, Qt::Key_Return);
  settle();
  const auto numeric_anchor_point = selected_anchor(overlay).value(QStringLiteral("point")).toList();
  if (numeric_anchor_point.size() < 2 ||
      std::abs(numeric_anchor_point.at(0).toDouble() - 0.825) > 0.0001 ||
      std::abs(numeric_anchor_point.at(1).toDouble() - numeric_y_before) > 0.0001)
    return fail("Apply the numeric X position in image percentage coordinates");
  if (!click_item(controls_window, finish_button) || commits.size() != 7)
    return fail("Commit numeric anchor placement through the visible Finish action");

  controls_window.requestActivate();
  settle();
  if (!click_item(controls_window, anchor_y_field) ||
      !QMetaObject::invokeMethod(anchor_y_field, "selectAll"))
    return fail("Focus the selected-anchor Y position field");
  type_numeric(controls_window, QStringLiteral("37.25"));
  QTest::keyClick(&controls_window, Qt::Key_Return);
  settle();
  const auto second_numeric_point = selected_anchor(overlay).value(QStringLiteral("point")).toList();
  if (second_numeric_point.size() < 2 ||
      std::abs(second_numeric_point.at(0).toDouble() - 0.825) > 0.0001 ||
      std::abs(second_numeric_point.at(1).toDouble() - 0.3725) > 0.0001)
    return fail("Apply the numeric Y position without changing the selected X coordinate");
  if (!click_item(controls_window, finish_button) || commits.size() != 8)
    return fail("Commit the numeric Y position through the visible Finish action");

  // Pending async selection can be cancelled by Escape while edits stay blocked.
  overlay->setProperty("requireCommitAcknowledgement", true);
  click(window, QPointF(184, 184));
  const auto escape_snapshot = json_value(overlay->property("transactionStart"));
  const auto escape_preview = json_document(overlay);
  if (!overlay->property("transactionOpen").toBool() ||
      !QMetaObject::invokeMethod(overlay, "finishPath") || commits.size() != 9 ||
      !overlay->property("commitPending").toBool() || !cancel_button->isEnabled())
    return fail("Wait for an async commit while keeping Cancel available");
  click(window, QPointF(96, 96));
  if (json_document(overlay) != escape_preview)
    return fail("Block pointer edits while an async commit is pending");
  key(window, Qt::Key_Escape);
  if (json_document(overlay) != escape_snapshot ||
      overlay->property("transactionOpen").toBool() ||
      overlay->property("commitPending").toBool() ||
      cancels.size() != 2)
    return fail("Escape cancels a pending async commit and restores the exact transaction snapshot");

  // The Cancel button takes the same path while an async commit is pending.
  click(window, QPointF(184, 184));
  const auto button_snapshot = json_value(overlay->property("transactionStart"));
  if (!QMetaObject::invokeMethod(overlay, "finishPath") || commits.size() != 10 ||
      !overlay->property("commitPending").toBool() || !click_item(controls_window, cancel_button) ||
      overlay->property("transactionOpen").toBool() ||
      overlay->property("commitPending").toBool() ||
      json_document(overlay) != button_snapshot || cancels.size() != 3)
    return fail("Cancel button cancels a pending async commit and restores its snapshot");

  // Rejection preserves edits and their original snapshot so Finish can retry.
  click(window, QPointF(184, 184));
  const auto retry_snapshot = json_value(overlay->property("transactionStart"));
  const auto retry_preview = json_document(overlay);
  if (!QMetaObject::invokeMethod(overlay, "finishPath") || commits.size() != 11 ||
      !overlay->property("commitPending").toBool() ||
      !QMetaObject::invokeMethod(overlay, "rejectCommit", Q_ARG(QVariant, QVariant(QStringLiteral("controller rejected")))) ||
      !overlay->property("transactionOpen").toBool() ||
      overlay->property("commitPending").toBool() ||
      json_value(overlay->property("transactionStart")) != retry_snapshot ||
      json_document(overlay) != retry_preview ||
      overlay->property("commitError").toString().isEmpty())
    return fail("A rejected controller commit preserves the path and transaction snapshot for retry");
  if (!click_item(controls_window, finish_button) || commits.size() != 12 ||
      !overlay->property("commitPending").toBool() ||
      !QMetaObject::invokeMethod(overlay, "acceptCommit") ||
      overlay->property("transactionOpen").toBool())
    return fail("Accept a retried controller commit explicitly");
  const auto before_exact_cancel = json_document(overlay);
  click(window, QPointF(18, 184));
  key(window, Qt::Key_Escape);
  if (json_document(overlay) != before_exact_cancel)
    return fail("Escape restores the exact document after an acknowledged retry workflow");
  overlay->setProperty("requireCommitAcknowledgement", false);

  // Style fields are panel inputs, separate from the editable path geometry.
  if (!QMetaObject::invokeMethod(controls, "publishAppearance"))
    return fail("Publish fill, stroke and stroke width from the path controls");
  if (appearance.size() != 1 || appearance.last().at(0).toString() != QStringLiteral("#4a78ff") ||
      appearance.last().at(1).toString() != QStringLiteral("#ffffff") ||
      appearance.last().at(2).toDouble() != 2.0)
    return fail("Send default vector fill, stroke and width through the appearance contract");

  auto* mask_target_label = find_item(controls, QStringLiteral("photoVectorPathMaskTargetLabel"));
  controls->setProperty("maskTarget", true);
  settle();
  if (!mask_target_label || !mask_target_label->isVisible() ||
      !overlay->property("requireClosedPathToFinish").toBool() ||
      !overlay->property("requireAllPathsClosedToFinish").toBool() ||
      layer_target->isVisible() || selection_target->isVisible() || fill_field->isVisible() ||
      stroke_field->isVisible() || width_input->isVisible())
    return fail("Mask mode identifies the target, requires a closed path and hides layer appearance controls");
  if (!overlay->property("hasClosedPath").toBool() ||
      overlay->property("hasOnlyClosedPaths").toBool() ||
      !QMetaObject::invokeMethod(overlay, "beginTransaction"))
    return fail("Prepare a document with a closed contour and an open contour for target gating");
  controls->setProperty("maskTarget", false);
  controls->setProperty("outputMode", QStringLiteral("selection"));
  settle();
  if (!overlay->property("requireClosedPathToFinish").toBool() ||
      overlay->property("requireAllPathsClosedToFinish").toBool() ||
      !overlay->property("canFinishPath").toBool())
    return fail("Selection finishing accepts a closed contour while ignoring open contours");
  controls->setProperty("maskTarget", true);
  settle();
  if (overlay->property("canFinishPath").toBool() || finish_button->isEnabled() ||
      !selection_guidance->isVisible() ||
      !selection_guidance->property("text").toString().contains(QStringLiteral("every subpath")))
    return fail("Mask finishing requires every subpath to be closed and explains the remaining work");
  controls->setProperty("maskTarget", false);
  controls->setProperty("outputMode", QStringLiteral("layer"));
  if (!QMetaObject::invokeMethod(overlay, "cancelPath"))
    return fail("Close the path-target gate fixture without committing it");
  controls->setProperty("maskTarget", false);
  settle();

  std::cout << "Photo Pen UI: pointer creation, cubic insertion, handles, numeric anchors, delete, retryable commits, cancellation and mask target controls passed\n";
  return 0;
}
