#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QMouseEvent>
#include <QJSValue>
#include <iostream>
#include "photo_text_document_style.h"
#include "photo_text_layout.h"
#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QPainter>

int main(int argc, char** argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QGuiApplication app(argc, argv);
  app.setFont(QFont(QStringLiteral("Helvetica Neue")));
  qmlRegisterType<photo_editor::desktop::PhotoTextDocumentStyle>("PhotoEditor", 1, 0, "PhotoTextDocumentStyle");
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components"
Item {
    id: harness
    width: 800; height: 600
    signal assetChanged()
    property int commits: 0
    property bool rejectEdits: false
    property int toolShortcuts: 0
    property var createdBox: []
    property var createdPoint: []
    property var createdRequest: ({})
    property int creations: 0
    property int pointCreations: 0
    property int styleCalls: 0
    property int coalescedBegins: 0
    property int coalescedEnds: 0
    property string createdText: ""
    property var createdStyle: ({})
    function addParagraphTextLayer(text, x, y, width, height, canvasWidth, canvasHeight) {
        if (rejectEdits) return false
        creations++; createdText = text; return true
    }
    function addPointTextLayer(text, x, y, canvasWidth, canvasHeight) {
        if (rejectEdits) return false
        pointCreations++; createdText = text; return true
    }
    function setPhotoLayerTextStyle(index, fontFamily, fontSize, color, face, alignment) {
        if (rejectEdits) return false
        styleCalls++
        createdStyle = {index: index, family: fontFamily, size: fontSize,
                        color: color, face: face, alignment: alignment}
        return true
    }
    function beginCoalescedEdit() { coalescedBegins++ }
    function endCoalescedEdit() { coalescedEnds++ }
    Shortcut { sequence: "F"; onActivated: harness.toolShortcuts++ }
    property string saved: ""
    function setPhotoLayerText(index, value) { if (rejectEdits) return false; commits++; saved = value; return true }
    PhotoTextCreateOverlay {
        objectName: "creator"
        anchors.fill: parent
        imageX: 40; imageY: 50; imageWidth: 400; imageHeight: 200
        canvasWidth: 800; canvasHeight: 400
        controlsParent: controlsHost
        onBoxRequested: function(x, y, width, height) { harness.createdBox = [x, y, width, height] }
        onPointRequested: function(x, y) { harness.createdPoint = [x, y] }
        onTextRequested: function(request) { harness.createdRequest = request }
    }
    Item { id: controlsHost; x: 620; y: 0; width: 180; height: 400 }
    PhotoTextOverlay {
        objectName: "overlay"
        documentId: "document-1"
        anchors.fill: parent
        controller: parent
        active: true
        layerIndex: 0
        layerData: ({id: "text-1", kind: "text", text: "Original", pixelWidth: 300, pixelHeight: 100})
        transformFrame: ({frameX: 20, frameY: 30, frameWidth: 300, frameHeight: 100, layerRotation: 0})
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_TEXT_TEST_ROOT "/tests/overlay-harness.qml")));
  std::unique_ptr<QObject> root(component.create());
  if (!root) { std::cerr << component.errorString().toStdString(); return 1; }
  QQuickWindow window;
  window.resize(800, 600);
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  window.show();
  window.requestActivate();
  QCoreApplication::processEvents();
  auto* overlay = root->findChild<QObject*>("overlay");
  auto* input = root->findChild<QObject*>("photoCanvasTextInput");
  if (!overlay || !input) return 1;
  QMetaObject::invokeMethod(overlay, "beginEditing");
  input->setProperty("text", "Discarded");
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, false));
  if (root->property("commits").toInt() != 0 || overlay->property("editing").toBool()) return 1;
  QMetaObject::invokeMethod(overlay, "beginEditing");
  input->setProperty("text", QString::fromUtf8("New caption\nCafé"));
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  if (root->property("commits").toInt() != 1 || root->property("saved") != input->property("text")) return 1;
  QMetaObject::invokeMethod(overlay, "beginEditing");
  input->setProperty("text", "Wrong target");
  overlay->setProperty("layerIndex", 1);
  if (overlay->property("editing").toBool() || root->property("commits").toInt() != 1) return 1;
  const auto key = [&](int code, Qt::KeyboardModifiers modifiers, const QString& text = {}) {
    QKeyEvent press(QEvent::KeyPress, code, modifiers, text);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, code, modifiers, text);
    QCoreApplication::sendEvent(&window, &release);
    QCoreApplication::processEvents();
  };
  qobject_cast<QQuickItem*>(root.get())->forceActiveFocus();
  key(Qt::Key_F, Qt::NoModifier, "f");
  if (root->property("toolShortcuts").toInt() != 1) {
    std::cerr << "Shortcut fixture must be active outside text editing\n";
    return 1;
  }
  overlay->setProperty("layerIndex", 0);
  QMetaObject::invokeMethod(overlay, "beginEditing");
  if (!overlay->property("textSelectionActive").toBool() ||
      overlay->property("textSelectionStart").toInt() != input->property("selectionStart").toInt() ||
      overlay->property("textSelectionEnd").toInt() != input->property("selectionEnd").toInt() ||
      !input->property("persistentSelection").toBool()) {
    std::cerr << "Canvas editing must expose its active selection and keep it visible while formatting controls take focus\n";
    return 1;
  }
  key(Qt::Key_F, Qt::NoModifier, "f");
  if (input->property("text").toString() != "f" || root->property("toolShortcuts").toInt() != 1) {
    std::cerr << "Typing must replace the selection without triggering tool shortcuts\n";
    return 1;
  }
  key(Qt::Key_Z, Qt::ControlModifier);
  if (input->property("text").toString() != "Original") {
    std::cerr << "Text editing must own local undo\n";
    return 1;
  }
  key(Qt::Key_Escape, Qt::NoModifier);
  if (overlay->property("editing").toBool() || root->property("commits").toInt() != 1) return 1;
  QMetaObject::invokeMethod(overlay, "beginEditing");
  QInputMethodEvent composition(QString::fromUtf8("かな"), {});
  QCoreApplication::sendEvent(input, &composition);
  if (root->property("commits").toInt() != 1) return 1;
  QInputMethodEvent commit;
  commit.setCommitString(QString::fromUtf8("仮名"));
  QCoreApplication::sendEvent(input, &commit);
  key(Qt::Key_Return, Qt::ControlModifier);
  if (overlay->property("editing").toBool() || root->property("commits").toInt() != 2 ||
      root->property("saved").toString() != QString::fromUtf8("仮名")) {
    std::cerr << "IME commit followed by Done must save the composed text once\n";
    return 1;
  }
  QMetaObject::invokeMethod(overlay, "beginEditing");
  const auto long_text = QStringLiteral("A line of text\n").repeated(30);
  input->setProperty("text", long_text);
  input->setProperty("cursorPosition", long_text.size());
  QCoreApplication::processEvents();
  const auto* viewport = root->findChild<QObject*>("photoCanvasTextViewport");
  if (!viewport || viewport->property("contentY").toDouble() <= 0) {
    std::cerr << "Overflowing drafts must scroll to the caret\n";
    return 1;
  }
  key(Qt::Key_Escape, Qt::NoModifier);
  QMetaObject::invokeMethod(overlay, "beginEditing");
  input->setProperty("text", "Outside click commits");
  const QPointF outside(700, 500);
  QMouseEvent down(QEvent::MouseButtonPress, outside, outside, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &down);
  QMouseEvent up(QEvent::MouseButtonRelease, outside, outside, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &up);
  if (overlay->property("editing").toBool() || root->property("commits").toInt() != 3 ||
      root->property("saved").toString() != "Outside click commits") {
    std::cerr << "Clicking outside the canvas text box must commit once\n";
    return 1;
  }
  auto* creator = root->findChild<QObject*>("creator");
  creator->setProperty("active", true);
  const auto mouse = [&](QEvent::Type type, QPointF point, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, point, point, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);
  };
  mouse(QEvent::MouseButtonPress, {340, 200}, Qt::LeftButton, Qt::LeftButton);
  mouse(QEvent::MouseMove, {140, 100}, Qt::NoButton, Qt::LeftButton);
  mouse(QEvent::MouseButtonRelease, {140, 100}, Qt::LeftButton, Qt::NoButton);
  const auto box = root->property("createdBox").value<QJSValue>().toVariant().toList();
  if (box != QVariantList{200, 100, 400, 200}) {
    std::cerr << "Reverse drag must create a normalized box in document pixels\n";
    return 1;
  }
  const auto paragraph_request = root->property("createdRequest").value<QJSValue>().toVariant().toMap();
  if (paragraph_request.value("x").toInt() != 200 || paragraph_request.value("y").toInt() != 100 ||
      paragraph_request.value("width").toInt() != 400 || paragraph_request.value("height").toInt() != 200 ||
      paragraph_request.value("pointText").toBool() || paragraph_request.value("text").toString() != "Text") {
    std::cerr << "Paragraph creation must carry its geometry and initial typography draft\n";
    return 1;
  }
  mouse(QEvent::MouseButtonPress, {100, 70}, Qt::LeftButton, Qt::LeftButton);
  mouse(QEvent::MouseButtonRelease, {100, 70}, Qt::LeftButton, Qt::NoButton);
  const auto clicked_box = root->property("createdBox").value<QJSValue>().toVariant().toList();
  if (root->property("createdPoint").value<QJSValue>().toVariant().toList() != QVariantList{120, 40} || clicked_box != box) return 1;
  const auto point_request = root->property("createdRequest").value<QJSValue>().toVariant().toMap();
  if (!point_request.value("pointText").toBool() || point_request.value("width").toInt() != 512 ||
      point_request.value("height").toInt() != 128 || point_request.value("canvasWidth").toInt() != 800 ||
      point_request.value("canvasHeight").toInt() != 400) {
    std::cerr << "Point creation must carry point-text mode and document dimensions\n";
    return 1;
  }
  mouse(QEvent::MouseButtonPress, {100, 70}, Qt::LeftButton, Qt::LeftButton);
  creator->setProperty("active", false);
  mouse(QEvent::MouseButtonRelease, {200, 170}, Qt::LeftButton, Qt::NoButton);
  if (root->property("createdBox").value<QJSValue>().toVariant().toList() != clicked_box) {
    std::cerr << "Leaving text creation must cancel an unfinished drag\n";
    return 1;
  }
  QMetaObject::invokeMethod(overlay, "beginEditing");
  input->setProperty("text", "Keep my draft");
  QMetaObject::invokeMethod(overlay, "beginEditing");
  if (input->property("text").toString() != "Keep my draft") return 1;
  root->setProperty("rejectEdits", true);
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  if (!overlay->property("editing").toBool() || overlay->property("editError").toString().isEmpty() ||
      root->property("commits").toInt() != 3 || input->property("text").toString() != "Keep my draft") {
    std::cerr << "Failed text commit must retain the draft for retry\n";
    return 1;
  }
  root->setProperty("rejectEdits", false);
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  if (overlay->property("editing").toBool() || root->property("commits").toInt() != 4) return 1;
  QMetaObject::invokeMethod(overlay, "beginEditing");
  input->setProperty("text", "Stale draft");
  overlay->setProperty("layerData", QVariantMap{{"id", "text-1"}, {"kind", "text"},
      {"text", "External revision"}, {"pixelWidth", 300}, {"pixelHeight", 100}});
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  if (overlay->property("editing").toBool() || root->property("commits").toInt() != 4) return 1;
  const QVariantMap creation_box{{"x", 120}, {"y", 80}, {"width", 300}, {"height", 160},
      {"canvasWidth", 800}, {"canvasHeight", 400}};
  const QVariantMap creation_view{{"imageX", 40}, {"imageY", 50}, {"imageWidth", 400}, {"imageHeight", 200}};
  QMetaObject::invokeMethod(overlay, "beginCreation", Q_ARG(QVariant, creation_box), Q_ARG(QVariant, creation_view));
  input->setProperty("text", "Discarded new layer");
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, false));
  if (root->property("creations").toInt() != 0) return 1;
  QMetaObject::invokeMethod(overlay, "beginCreation", Q_ARG(QVariant, creation_box), Q_ARG(QVariant, creation_view));
  input->setProperty("text", "New paragraph");
  root->setProperty("rejectEdits", true);
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  if (!overlay->property("editing").toBool() || root->property("creations").toInt() != 0) return 1;
  root->setProperty("rejectEdits", false);
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  if (overlay->property("editing").toBool() || root->property("creations").toInt() != 1 ||
      root->property("createdText").toString() != "New paragraph") {
    std::cerr << "New paragraph drafts must create one layer only on successful Done\n";
    return 1;
  }
  QMetaObject::invokeMethod(overlay, "beginCreation", Q_ARG(QVariant, creation_box), Q_ARG(QVariant, creation_view));
  input->setProperty("text", "Draft for the previous photo");
  overlay->setProperty("documentId", "document-2");
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  if (overlay->property("editing").toBool() || root->property("creations").toInt() != 1)
    return 1;
  QMetaObject::invokeMethod(overlay, "beginEditing");
  input->setProperty("text", "Stale existing-layer draft");
  overlay->setProperty("documentId", "document-3");
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  if (overlay->property("editing").toBool() || root->property("commits").toInt() != 4) {
    std::cerr << "Document switching must discard both creation and existing-layer drafts\n";
    return 1;
  }
  QMetaObject::invokeMethod(overlay, "beginCreation", Q_ARG(QVariant, creation_box), Q_ARG(QVariant, creation_view));
  QMetaObject::invokeMethod(root.get(), "assetChanged");
  if (overlay->property("editing").toBool() || root->property("creations").toInt() != 1) return 1;

  photo_editor::desktop::PhotoTextStyle style;
  style.size = 28.5;
  style.line_height = 1.8;
  style.paragraph_spacing = 17.25;
  style.baseline_offset = -7;
  style.color = QColor("#e78c39");

  const QVariantMap point_creation{{"x", 120}, {"y", 80}, {"width", 512}, {"height", 128},
      {"canvasWidth", 800}, {"canvasHeight", 400}, {"pointText", true},
      {"text", "Styled point"}, {"fontFamily", "Lyskamm"}, {"fontSize", 52}, {"color", "#80445566"}};
  QMetaObject::invokeMethod(overlay, "beginCreation", Q_ARG(QVariant, point_creation), Q_ARG(QVariant, creation_view));
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  const auto created_style = root->property("createdStyle").value<QJSValue>().toVariant().toMap();
  if (overlay->property("editing").toBool() || root->property("pointCreations").toInt() != 1 ||
      root->property("styleCalls").toInt() != 1 || root->property("createdText").toString() != "Styled point" ||
      created_style.value("index").toInt() != 0 || created_style.value("family").toString() != "Lyskamm" ||
      created_style.value("size").toDouble() != 52 || created_style.value("color").toString() != "#80445566" ||
      root->property("coalescedBegins").toInt() != root->property("coalescedEnds").toInt()) {
    std::cerr << "Point text creation must commit its initial typography in one coalesced edit\n";
    return 1;
  }

  QMetaObject::invokeMethod(overlay, "beginEditing");
  input->setProperty("text", "Draft during style update");
  auto style_update = QVariantMap{{"id", "text-1"}, {"kind", "text"}, {"text", "External revision"},
      {"pixelWidth", 300}, {"pixelHeight", 100},
      {"textTypography", photo_editor::desktop::encode_photo_text_style(style).toVariantMap()}};
  overlay->setProperty("layerData", style_update);
  if (!overlay->property("editing").toBool() || input->property("text").toString() != "Draft during style update" ||
      input->property("color").value<QColor>() != style.color) {
    std::cerr << "Style-only updates must refresh the draft style without discarding typed text\n";
    return 1;
  }
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, true));
  if (root->property("commits").toInt() != 5 || root->property("saved").toString() != "Draft during style update") {
    std::cerr << "A style-only update must preserve the pending text commit\n";
    return 1;
  }

  photo_editor::desktop::PhotoTextStyle path_style;
  path_style.family = QStringLiteral("Georgia");
  path_style.size = 29.5;
  path_style.tracking = 2.25;
  path_style.path_points.reserve(200);
  for (int index = 0; index < 200; ++index)
    path_style.path_points.push_back(QPointF(static_cast<qreal>(index) / 199.0, 0.5));
  QVariantMap path_layer{{"id", "path-text"}, {"kind", "text"}, {"text", "Text on a path"},
      {"pixelWidth", 300}, {"pixelHeight", 100},
      {"textTypography", photo_editor::desktop::encode_photo_text_style(path_style).toVariantMap()}};
  overlay->setProperty("layerData", path_layer);
  overlay->setProperty("pathHandlesActive", true);
  QCoreApplication::processEvents();
  const auto find_named_item = [&](auto&& self, QQuickItem* item, const QString& name) -> QQuickItem* {
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    for (auto* child : item->childItems())
      if (auto* match = self(self, child, name)) return match;
    return nullptr;
  };
  auto* first_path_handle = find_named_item(find_named_item,
      qobject_cast<QQuickItem*>(overlay), QStringLiteral("photoTextPathHandle_0"));
  if (!first_path_handle || overlay->property("pathHandleCount").toInt() != 64 ||
      !overlay->property("styledText").toBool() || !overlay->property("straightTextDraftNotice").toBool()) {
    std::cerr << "Large path state: handle=" << (first_path_handle != nullptr)
        << " count=" << overlay->property("pathHandleCount").toInt()
        << " styled=" << overlay->property("styledText").toBool()
        << " notice=" << overlay->property("straightTextDraftNotice").toBool() << '\n';
    return 1;
  }
  path_style.path_points[0].setX(0.15);
  path_layer.insert("textTypography", photo_editor::desktop::encode_photo_text_style(path_style).toVariantMap());
  overlay->setProperty("layerData", path_layer);
  QCoreApplication::processEvents();
  if (find_named_item(find_named_item, qobject_cast<QQuickItem*>(overlay),
          QStringLiteral("photoTextPathHandle_0")) != first_path_handle) {
    std::cerr << "Moving a path point must preserve handle delegates during the drag\n";
    return 1;
  }
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, false));

  photo_editor::desktop::PhotoTextStyle variable_style;
  variable_style.axes.insert(QStringLiteral("wght"), 650.0);
  const QVariantMap variable_layer{{"id", "variable-text"}, {"kind", "text"},
      {"text", "Variable font"}, {"pixelWidth", 300}, {"pixelHeight", 100},
      {"textTypography", photo_editor::desktop::encode_photo_text_style(variable_style).toVariantMap()}};
  overlay->setProperty("layerData", variable_layer);
  QMetaObject::invokeMethod(overlay, "beginEditing");
  QCoreApplication::processEvents();
  const auto variable_font = input->property("font").value<QFont>();
  const auto weight_axis = QFont::Tag::fromString("wght");
  if (!overlay->property("styledText").toBool() || !weight_axis ||
      !variable_font.isVariableAxisSet(*weight_axis) ||
      std::abs(variable_font.variableAxisValue(*weight_axis) - 650.0f) > 0.1f) {
    std::cerr << "Version-4 variable font axes must reach the canvas draft font\n";
    return 1;
  }
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, false));

  const QVariantMap styled_layer{{"id", "styled"}, {"kind", "text"},
      {"text", "Golden text\nSecond line"}, {"pixelWidth", 360}, {"pixelHeight", 180},
      {"textTypography", photo_editor::desktop::encode_photo_text_style(style).toVariantMap()}};
  overlay->setProperty("layerData", styled_layer);
  overlay->setProperty("transformFrame", QVariantMap{{"frameX", 40}, {"frameY", 40},
      {"frameWidth", 360}, {"frameHeight", 180}, {"layerRotation", 0}});
  QMetaObject::invokeMethod(overlay, "beginEditing");
  QCoreApplication::processEvents();
  if (input->property("color").value<QColor>() != style.color) {
    std::cerr << "Canvas text color=" << input->property("color").value<QColor>().name().toStdString() << " expected=" << style.color.name().toStdString() << "\n";
    return 1;
  }
  auto* quick_document = qvariant_cast<QQuickTextDocument*>(input->property("textDocument"));
  if (!quick_document) return 1;
  auto* document = quick_document->textDocument();
  auto first_block = document->begin();
  auto second_block = first_block.next();
  const auto first_rect = document->documentLayout()->blockBoundingRect(first_block);
  const auto second_rect = document->documentLayout()->blockBoundingRect(second_block);
  const auto advance = second_rect.y() - first_rect.y();
  std::cout << "draft_font_px=" << QFontInfo(document->defaultFont()).pixelSize()
      << " line_y=" << first_block.layout()->lineAt(0).y()
      << " line_height=" << first_block.layout()->lineAt(0).height()
      << " line_ascent=" << first_block.layout()->lineAt(0).ascent()
      << " rect_y=" << first_rect.y() << '\n';
  if (std::abs(advance - style.size * style.line_height - style.paragraph_spacing) > 0.1) {
    std::cerr << "Canvas paragraph advance=" << advance << " expected=" << style.size * style.line_height + style.paragraph_spacing << '\n';
    return 1;
  }
  const auto committed = photo_editor::desktop::rasterize_photo_text(
      styled_layer.value("text").toString(), style, QSizeF(360, 180), QSize(360, 180));
  QImage draft(360, 180, QImage::Format_ARGB32_Premultiplied);
  draft.fill(Qt::transparent);
  QPainter painter(&draft);
  painter.translate(0, input->property("y").toDouble());
  QAbstractTextDocumentLayout::PaintContext context;
  context.palette.setColor(QPalette::Text, style.color);
  document->documentLayout()->draw(&painter, context);
  painter.end();
  // Compare actual glyph support, not just property bindings or source strings.
  int mismatched = 0;
  int ink = 0;
  for (int y = 0; y < draft.height(); ++y) for (int x = 0; x < draft.width(); ++x) {
    const auto a = qAlpha(draft.pixel(x, y));
    const auto b = qAlpha(committed.image.pixel(x, y));
    if (a > 32 || b > 32) ++ink;
    if ((a > 32) != (b > 32)) ++mismatched;
  }
  std::cout << "text_draft_ink=" << ink << " coverage_disagreement=" << mismatched << '\n';
  const auto proof_dir = qEnvironmentVariable("PHOTO_EDITOR_TEXT_PROOF_DIR");
  if (!proof_dir.isEmpty()) {
    QDir().mkpath(proof_dir);
    draft.save(proof_dir + "/text-draft.png");
    committed.image.save(proof_dir + "/text-committed.png");
    QMetaObject::invokeMethod(input, "deselect");
    QCoreApplication::processEvents();
    window.grabWindow().save(proof_dir + "/text-window.png");
  }
  if (ink == 0 || mismatched > ink / 20) {
    std::cerr << "Canvas document glyph layout must agree with the committed renderer\n";
    return 1;
  }

  style.material = QStringLiteral("soft_emboss");
  const QVariantMap material_layer{{"id", "material"}, {"kind", "text"},
      {"text", "Material text"}, {"pixelWidth", 360}, {"pixelHeight", 180},
      {"textTypography", photo_editor::desktop::encode_photo_text_style(style).toVariantMap()}};
  overlay->setProperty("layerData", material_layer);
  QMetaObject::invokeMethod(overlay, "beginEditing");
  QCoreApplication::processEvents();
  auto* draft_notice = overlay->findChild<QObject*>("photoTextDraftStyleNotice");
  const auto draft_font = input->property("font").value<QFont>();
  if (!overlay->property("styledText").toBool() || !draft_notice || !draft_notice->property("visible").toBool() ||
      draft_font.family() != style.family) {
    std::cerr << "Material text drafts must retain the saved base font and explain the preview fallback\n";
    return 1;
  }
  QMetaObject::invokeMethod(overlay, "finish", Q_ARG(QVariant, false));

  auto point_material_style = style;
  point_material_style.family = QStringLiteral("Georgia");
  point_material_style.size = 31.5;
  point_material_style.point_text = true;
  const QVariantMap point_material_layer{{"id", "material-point"}, {"kind", "text"},
      {"text", "Material point"}, {"pixelWidth", 512}, {"pixelHeight", 128},
      {"textTypography", photo_editor::desktop::encode_photo_text_style(point_material_style).toVariantMap()}};
  overlay->setProperty("layerData", point_material_layer);
  QMetaObject::invokeMethod(overlay, "beginEditing");
  QCoreApplication::processEvents();
  const auto logical_width = overlay->property("logicalWidth").toDouble();
  const auto logical_height = overlay->property("logicalHeight").toDouble();
  const auto point_geometry = overlay->property("pointGeometry").value<QVariantMap>();
  const auto point_draft_font = input->property("font").value<QFont>();
  if (!overlay->property("validPointGeometry").toBool() || logical_width <= 0 || logical_height <= 0 ||
      !point_geometry.value("error").toString().isEmpty() || point_draft_font.family() != point_material_style.family) {
    std::cerr << "Version-8 point text must measure valid draft bounds with its saved base font\n";
    return 1;
  }
  return 0;
}
