#include <cstdlib>
#include <iostream>

#include <QGuiApplication>
#include <QFile>
#include <QMetaObject>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QQuickItem>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QVariantList>

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QString read_production_qml() {
  QFile file(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return {};
  return QString::fromUtf8(file.readAll());
}

QString unique_property_expression(const QString& block,
                                  const QString& property,
                                  int* matches) {
  const QRegularExpression expression(
      QStringLiteral("(?m)^\\s*%1:\\s*(.+?)\\s*$").arg(QRegularExpression::escape(property)));
  auto iterator = expression.globalMatch(block);
  QString result;
  *matches = 0;
  while (iterator.hasNext()) {
    const auto match = iterator.next();
    ++*matches;
    result = match.captured(1).trimmed();
  }
  return result;
}

QString production_group_visibility(const QString& source) {
  const QString id = QStringLiteral("id: activeLayerMaskChildrenGroup");
  if (source.count(id) != 1)
    return {};
  const auto start = source.indexOf(id);
  if (start < 0 || source.count(id) != 1)
    return {};
  const auto end = source.indexOf(QStringLiteral("title: \"Linked Selections\""), start);
  if (end < 0)
    return {};
  const QString block = source.mid(start, end - start);
  int matches = 0;
  const QString value = unique_property_expression(block, QStringLiteral("visible"), &matches);
  return matches == 1 ? value : QString{};
}

QString production_repeater_model(const QString& source) {
  if (source.count(QStringLiteral("id: activeLayerMaskChildrenGroup")) != 1)
    return {};
  const auto group_start = source.indexOf(QStringLiteral("id: activeLayerMaskChildrenGroup"));
  if (group_start < 0)
    return {};
  const auto repeater_start = source.indexOf(QStringLiteral("Repeater {"), group_start);
  const auto delegate_start = source.indexOf(QStringLiteral("delegate: Rectangle {"), repeater_start);
  if (repeater_start < 0 || delegate_start < 0)
    return {};
  const QString block = source.mid(repeater_start, delegate_start - repeater_start);
  const QRegularExpression model_start(QStringLiteral("(?m)^\\s*model:\\s*(.*)$"));
  auto iterator = model_start.globalMatch(block);
  if (!iterator.hasNext())
    return {};
  const auto match = iterator.next();
  if (iterator.hasNext())
    return {};
  const auto line_start = block.lastIndexOf(QLatin1Char('\n'), match.capturedStart()) + 1;
  const QString remaining = block.mid(line_start);
  QStringList expression_lines;
  const auto lines = remaining.split(QLatin1Char('\n'));
  bool started = false;
  for (const QString& line : lines) {
    const QString trimmed = line.trimmed();
    if (!started) {
      if (!trimmed.startsWith(QStringLiteral("model:")))
        continue;
      started = true;
      expression_lines.push_back(trimmed.mid(QStringLiteral("model:").size()).trimmed());
      continue;
    }
    if (trimmed.isEmpty() || trimmed.startsWith(QStringLiteral("delegate:")))
      break;
    if (trimmed.startsWith(QStringLiteral("//")))
      return {};
    expression_lines.push_back(trimmed);
  }
  if (expression_lines.isEmpty())
    return {};
  return expression_lines.join(QLatin1Char(' ')).trimmed();
}

QString production_edit_action(const QString& source) {
  const QString button_id = QStringLiteral("objectName: \"activeLayerMaskChildEditMaskButton\"");
  if (source.count(button_id) != 1)
    return {};
  const auto button_start = source.indexOf(button_id);
  if (button_start < 0 || source.count(QStringLiteral("objectName: \"activeLayerMaskChildEditMaskButton\"")) != 1)
    return {};
  const auto button_end = source.indexOf(QLatin1Char('}'), button_start);
  if (button_end < 0)
    return {};
  const QString block = source.mid(button_start, button_end - button_start);
  int matches = 0;
  const QString value = unique_property_expression(block, QStringLiteral("onClicked"), &matches);
  return matches == 1 ? value : QString{};
}

QVariantMap row(const QString& id, const QString& title) {
  return {{QStringLiteral("id"), id}, {QStringLiteral("name"), title}};
}

QVariant invoke(const QObject* object, const char* method, int index = -1) {
  QVariant result;
  const QVariant argument(index);
  const bool ok = index < 0
      ? QMetaObject::invokeMethod(const_cast<QObject*>(object), method,
                                  Q_RETURN_ARG(QVariant, result))
      : QMetaObject::invokeMethod(const_cast<QObject*>(object), method,
                                  Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, argument));
  if (!ok)
    return {};
  return result;
}

void process_qml() {
  QGuiApplication::processEvents();
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  const QString source = read_production_qml();
  if (source.isEmpty())
    return fail(QStringLiteral("could not read production StudioWorkspace.qml"));
  const QString visibility = production_group_visibility(source);
  const QString model = production_repeater_model(source);
  const QString edit_action = production_edit_action(source);
  if (visibility.isEmpty() || model.isEmpty() || edit_action.isEmpty())
    return fail(QStringLiteral("expected exactly one production linked-mask visibility, Repeater model, and Edit Selection action expression"));

  const QString qml = QStringLiteral(
      "import QtQuick\n"
      "Item {\n"
      "  id: root\n"
      "  width: 400; height: 400\n"
      "  property bool parentShown: false\n"
      "  property bool targetActive: true\n"
      "  property var rowPayloads: []\n"
      "  property string lastSelectedId: \"\"\n"
      "  property bool lastFocusFlag: true\n"
      "  property bool lastSelectLayerFlag: false\n"
      "  property int childCount: linkedMaskRepeater.count\n"
      "  function layerTargetInspectorActive() { return targetActive }\n"
      "  function selectMaskSetAndLayer(rowData, focusTarget, selectLayer) {\n"
      "    lastSelectedId = String(rowData.id || \"\")\n"
      "    lastFocusFlag = focusTarget\n"
      "    lastSelectLayerFlag = selectLayer\n"
      "  }\n"
      "  function childId(index) {\n"
      "    const child = linkedMaskRepeater.itemAt(index)\n"
      "    return child ? child.rowId : \"\"\n"
      "  }\n"
      "  function childName(index) {\n"
      "    const child = linkedMaskRepeater.itemAt(index)\n"
      "    return child ? child.rowName : \"\"\n"
      "  }\n"
      "  function activateChild(index) {\n"
      "    const child = linkedMaskRepeater.itemAt(index)\n"
      "    if (child) child.activate()\n"
      "  }\n"
      "  Item {\n"
      "    id: hiddenParent\n"
      "    objectName: \"hiddenParent\"\n"
      "    visible: root.parentShown\n"
      "    anchors.fill: parent\n"
      "    Item {\n"
      "      id: activeLayerMaskChildrenGroup\n"
      "      objectName: \"activeLayerMaskChildrenGroup\"\n"
      "      property var linkedMaskSets: root.rowPayloads\n"
      "      anchors.fill: parent\n"
      "      visible: %1\n"
      "      Repeater {\n"
      "        id: linkedMaskRepeater\n"
      "        model: %2\n"
      "        delegate: Item {\n"
      "          objectName: \"activeLayerMaskChildRow\"\n"
      "          required property var modelData\n"
      "          property string rowId: String(modelData.id || \"\")\n"
      "          property string rowName: String(modelData.name || \"\")\n"
      "          function activate() { %3 }\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}").arg(visibility, model, edit_action);

  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(qml.toUtf8(), QUrl(QStringLiteral("qrc:/MaskHiddenChildrenLifecycleProbe.qml")));
  QObject* object = component.create();
  if (!object) {
    QStringList errors;
    for (const auto& error : component.errors())
      errors.push_back(error.toString());
    return fail(QStringLiteral("actual-expression QML probe failed to instantiate: %1")
                    .arg(errors.join(QLatin1Char('\n'))));
  }
  const auto cleanup = [&]() { delete object; };
  const auto set = [&](const char* property, const QVariant& value) {
    const bool ok = object->setProperty(property, value);
    process_qml();
    return ok;
  };
  const auto number = [&](const char* property) { return object->property(property).toInt(); };
  const auto string = [&](const char* property) { return object->property(property).toString(); };

  if (!set("rowPayloads", QVariantList{row(QStringLiteral("a"), QStringLiteral("Alpha")),
                                        row(QStringLiteral("b"), QStringLiteral("Beta"))}) ||
      !set("targetActive", true) || !set("parentShown", false)) {
    cleanup();
    return fail(QStringLiteral("could not seed hidden-parent case"));
  }
  if (number("childCount") != 0 || object->property("parentShown").toBool() ||
      !object->property("targetActive").toBool()) {
    cleanup();
    return fail(QStringLiteral("hidden ancestor must suppress rows without changing target state"));
  }

  if (!set("parentShown", true) || number("childCount") != 2 ||
      invoke(object, "childId", 0).toString() != QStringLiteral("a") ||
      invoke(object, "childId", 1).toString() != QStringLiteral("b") ||
      invoke(object, "childName", 0).toString() != QStringLiteral("Alpha")) {
    cleanup();
    return fail(QStringLiteral("opening linked-mask parent must materialize current ordered rows"));
  }

  if (!set("targetActive", false) || number("childCount") != 0 ||
      !object->property("parentShown").toBool() || object->property("targetActive").toBool()) {
    cleanup();
    return fail(QStringLiteral("false production group predicate must hide rows and preserve parent state"));
  }
  const QVariantList same_count_update{
      row(QStringLiteral("b2"), QStringLiteral("Beta revised")),
      row(QStringLiteral("a2"), QStringLiteral("Alpha revised"))};
  if (!set("rowPayloads", same_count_update) || number("childCount") != 0) {
    cleanup();
    return fail(QStringLiteral("payload updates while hidden must remain unmaterialized"));
  }

  if (!set("targetActive", true) || number("childCount") != 2 ||
      invoke(object, "childId", 0).toString() != QStringLiteral("b2") ||
      invoke(object, "childName", 0).toString() != QStringLiteral("Beta revised") ||
      invoke(object, "childId", 1).toString() != QStringLiteral("a2")) {
    cleanup();
    return fail(QStringLiteral("reopening must materialize the latest same-count payload in order"));
  }
  const QVariant action_index(0);
  if (!QMetaObject::invokeMethod(object, "activateChild", Q_ARG(QVariant, action_index))) {
    cleanup();
    return fail(QStringLiteral("could not invoke current child action"));
  }
  process_qml();
  if (string("lastSelectedId") != QStringLiteral("b2") ||
      object->property("lastFocusFlag").toBool() ||
      !object->property("lastSelectLayerFlag").toBool() ||
      !object->property("parentShown").toBool() ||
      !object->property("targetActive").toBool()) {
    cleanup();
    return fail(QStringLiteral("production Edit Selection action must receive latest row payload without changing visibility state"));
  }

  if (!set("parentShown", false) || number("childCount") != 0 ||
      !object->property("targetActive").toBool()) {
    cleanup();
    return fail(QStringLiteral("closing ancestor must clear delegates while preserving active target"));
  }
  const QVariantList while_parent_hidden{
      row(QStringLiteral("latest-1"), QStringLiteral("Newest first")),
      row(QStringLiteral("latest-2"), QStringLiteral("Newest second")),
      row(QStringLiteral("latest-3"), QStringLiteral("Newest third"))};
  if (!set("rowPayloads", while_parent_hidden) || number("childCount") != 0 ||
      !object->property("targetActive").toBool()) {
    cleanup();
    return fail(QStringLiteral("hidden ancestor updates must not create rows or mutate target state"));
  }
  if (!set("parentShown", true) || number("childCount") != 3 ||
      invoke(object, "childId", 0).toString() != QStringLiteral("latest-1") ||
      invoke(object, "childId", 1).toString() != QStringLiteral("latest-2") ||
      invoke(object, "childName", 2).toString() != QStringLiteral("Newest third")) {
    cleanup();
    return fail(QStringLiteral("reopening ancestor must show the newest rows and ordering"));
  }

  cleanup();
  return EXIT_SUCCESS;
}
