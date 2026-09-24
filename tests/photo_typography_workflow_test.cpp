#include "app_bootstrap.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "photo_text_document_style.h"
#include "photo_text_rasterizer.h"
#include "photo_render_utils.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QSet>
#include <iostream>
#include <functional>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& controller) { return controller.currentState(); }
};
}
using namespace photo_editor::desktop;
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)
static bool wait_until(const std::function<bool()>& predicate) {
  QElapsedTimer timer; timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents,10);
    if (predicate()) return true;
    QThread::msleep(5);
  } while(timer.elapsed()<20000);
  return false;
}

int main(int argc, char** argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE","Basic");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE","1");
  QGuiApplication app(argc,argv);
  qmlRegisterType<PhotoTextDocumentStyle>("PhotoEditor",1,0,"PhotoTextDocumentStyle");
  QTemporaryDir temp;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",temp.path().toUtf8());
  CHECK(AppBootstrap::ensureRuntimeDirectories(),"Runtime directories");
  const auto path=qEnvironmentVariable("PHOTO_TEXT_TEST_SOURCE");
  QImage source=path.isEmpty()?QImage{}:QImage(path);
  const bool photographic=!source.isNull();
  CHECK(path.isEmpty() || photographic,"Photograph must load");
  if (photographic) source=source.scaled(640,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
  else {source=QImage(640,512,QImage::Format_RGB32);source.fill(QColor("#304050"));}
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path=temp.filePath("source.png");CHECK(source.save(source_path),"Source fixture");
  QQmlEngine engine;
  auto* provider=new EditorPreviewImageProvider;
  engine.addImageProvider("editor-preview",provider);
  auto controller=std::make_unique<EditorPreviewController>();
  controller->setImageProvider(provider);
  controller->loadAsset("typography-proof",source_path,{},{},{},{},{});
  CHECK(wait_until([&]{return !controller->displayPreviewSource().isEmpty() && !controller->processing() && !controller->fullRenderRunning();}),"Source preview");
  const auto state=[&]{return EditorPreviewControllerTestAccess::state(*controller);};
  engine.rootContext()->setContextProperty("testController",controller.get());
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQuick.Controls
import "../apps/desktop/qml/components"
Rectangle {
    id: harness
    width: 1024; height: 650; color: "#20242b"
    property bool creating: true
    readonly property var selectedLayer: testController.photoLayers.length ? testController.photoLayers[0] : ({})
    Image { x: 340; y: 20; width: 640; height: 512; source: testController.displayPreviewSource; cache: false }
    ScrollView {
        x: 12; y: 12; width: 305; height: 620; clip: true
        PhotoTypographyPanel {
            width: 285
            controller: testController; layerIndex: 0; layerData: harness.selectedLayer
            visible: testController.photoLayerCount > 0
            expanded: true
        }
    }
    PhotoTextCreateOverlay {
        id: creator
        anchors.fill: parent
        active: harness.creating && !editor.editing
        imageX: 340; imageY: 20; imageWidth: 640; imageHeight: 512
        canvasWidth: 640; canvasHeight: 512
        onPointRequested: function(x,y) {
            editor.beginCreation({x:x,y:y,width:512,height:128,pointText:true,canvasWidth:640,canvasHeight:512},creator)
        }
        onBoxRequested: function(x,y,width,height) {
            editor.beginCreation({x:x,y:y,width:width,height:height,canvasWidth:640,canvasHeight:512},creator)
        }
    }
    PhotoTextOverlay {
        id: editor
        objectName: "textEditor"
        anchors.fill: parent
        controller: testController; layerIndex: 0; layerData: harness.selectedLayer
        documentId: "typography-proof"; active: true
        transformFrame: ({frameX: 340+Number(harness.selectedLayer.x||0),frameY:20+Number(harness.selectedLayer.y||0),
            frameWidth:Number(harness.selectedLayer.pixelWidth||512),frameHeight:Number(harness.selectedLayer.pixelHeight||128),layerRotation:0})
        onEditFinished: function(committed,created) { harness.creating=false }
    }
}
)QML",QUrl::fromLocalFile(QStringLiteral(PHOTO_TEXT_TEST_ROOT "/tests/typography-workflow.qml")));
  std::unique_ptr<QObject> root(component.create());CHECK(root,component.errorString().toStdString());
  QQuickWindow window;window.resize(1024,650);
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());window.show();window.requestActivate();
  CHECK(wait_until([&]{return window.isExposed();}),"Native window exposed");
  const auto settle=[] {QElapsedTimer timer;timer.start();while(timer.elapsed()<150)QCoreApplication::processEvents(QEventLoop::AllEvents,10);};
  const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,p,window.mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);
    QCoreApplication::sendEvent(&window,&event);QCoreApplication::processEvents();
  };
  const auto click=[&](QPointF p) {mouse(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);settle();};
  const auto key=[&](int code,Qt::KeyboardModifiers modifiers,const QString& text=QString()) {
    QKeyEvent event(QEvent::KeyPress,code,modifiers,text);QCoreApplication::sendEvent(&window,&event);settle();
  };
  const auto find=[&](const QString& name) {
    QSet<QObject*> visited;
    std::function<QObject*(QObject*)> search=[&](QObject* object)->QObject* {
      if(!object || visited.contains(object))return nullptr;visited.insert(object);
      if(object->objectName()==name)return object;
      for(auto* child:object->children())if(auto* found=search(child))return found;
      if(auto* item=qobject_cast<QQuickItem*>(object))for(auto* child:item->childItems())if(auto* found=search(child))return found;
      return nullptr;
    };
    return search(root.get());
  };
  auto* editor=find("textEditor");auto* input=find("photoCanvasTextInput");CHECK(editor && input,"Text controls");
  const auto evidence=qEnvironmentVariable("PHOTO_TEXT_EVIDENCE_DIR");
  if(!evidence.isEmpty())CHECK(QDir().mkpath(evidence),"Evidence directory");
  click({390,110});CHECK(editor->property("editing").toBool(),"Native click begins point text");
  key(Qt::Key_unknown,Qt::NoModifier,"City light");
  const auto first_width=editor->property("logicalWidth").toDouble();
  key(Qt::Key_unknown,Qt::NoModifier," / evening");
  CHECK(editor->property("logicalWidth").toDouble()>first_width,"Point draft expands while typing");
  if(!evidence.isEmpty())CHECK(window.grabWindow().save(evidence+"/point-draft-native.png"),"Point draft screenshot");
  key(Qt::Key_Return,Qt::ControlModifier);
  CHECK(!editor->property("editing").toBool() && controller->photoLayerCount()==1,"Native point Done creates one layer");
  auto point=state().layers[0].toMap();
  CHECK(point.value("text")=="City light / evening" && point.value("textTypography").toMap().value("mode")=="point",
      "Point text stores unwrapped mode");
  CHECK(controller->photoLayerTextLayoutInfo(0).value("lineCount").toInt()==1,"Point text stays on one line");
  const auto point_state=state().layers;
  controller->undo();CHECK(controller->photoLayerCount()==0,"Point creation is one undo");
  controller->redo();CHECK(state().layers==point_state,"Redo restores point bounds");
  QMetaObject::invokeMethod(editor,"beginEditing");key(Qt::Key_unknown,Qt::NoModifier,"Discard this");key(Qt::Key_Escape,Qt::NoModifier);
  CHECK(state().layers==point_state,"Escape discards draft and its resized bounds");
  QMetaObject::invokeMethod(editor,"beginEditing");
  QInputMethodEvent commit;commit.setCommitString(QString::fromUtf8("Café 日本語"));QCoreApplication::sendEvent(input,&commit);
  key(Qt::Key_Return,Qt::ControlModifier);
  CHECK(state().layers[0].toMap().value("text").toString()==QString::fromUtf8("Café 日本語"),"Composed text commits through the controller");
  controller->undo();CHECK(state().layers==point_state,"Text content and fitted bounds undo together");
  CHECK(controller->setPhotoLayerTypography(0,{{"size",34.5},{"color","#ffeaa251"},{"italic",true}}),"Fractional point typography");
  CHECK(controller->photoLayerTextLayoutInfo(0).value("lineCount").toInt()==1,"Point remains unwrapped after typography change");
  CHECK(wait_until([&]{return !controller->processing()&&!controller->fullRenderRunning();}),"Point preview settles");
  if(!evidence.isEmpty()){settle();CHECK(window.grabWindow().save(evidence+"/point-native.png"),"Point committed screenshot");}
  const auto auto_sized=state().layers;
  CHECK(controller->setPhotoLayerTextBox(0,120,200),"Resize point text into a paragraph box");
  CHECK(controller->photoLayerTypography(0).value("mode").toString()!="point" &&
      controller->photoLayerTextLayoutInfo(0).value("lineCount").toInt()>1,"Explicit box size enables paragraph wrapping");
  controller->undo();CHECK(state().layers==auto_sized,"Undo box conversion restores point mode and bounds");
  root->setProperty("creating",true);settle();
  mouse(QEvent::MouseButtonPress,{390,270},Qt::LeftButton,Qt::LeftButton);
  mouse(QEvent::MouseMove,{900,490},Qt::NoButton,Qt::LeftButton);
  mouse(QEvent::MouseButtonRelease,{900,490},Qt::LeftButton,Qt::NoButton);settle();
  CHECK(editor->property("editing").toBool(),"Native drag begins paragraph text");
  key(Qt::Key_unknown,Qt::NoModifier,"A city in motion\nA quiet moment between the buildings.");key(Qt::Key_Return,Qt::ControlModifier);
  CHECK(controller->photoLayerCount()==2,"Paragraph creation adds a separate editable layer");
  CHECK(controller->setPhotoLayerTypography(0,{{"size",25.5},{"lineHeight",1.4},{"color","#ffffffff"}}),"Paragraph typography");
  settle();
  const auto before_spacing=state().layers;
  auto* spacing=qobject_cast<QQuickItem*>(find("photoTypography_paragraphSpacing"));CHECK(spacing && spacing->isVisible(),"Paragraph spacing control visible");
  click(spacing->mapToScene(QPointF(spacing->width()/2,spacing->height()/2)));
  CHECK(spacing->hasActiveFocus(),"Paragraph spacing field receives native focus");
  QMetaObject::invokeMethod(spacing,"selectAll");key(Qt::Key_unknown,Qt::NoModifier,"18.25");key(Qt::Key_Return,Qt::NoModifier);
  CHECK(controller->photoLayerTypography(0).value("paragraphSpacing").toDouble()==18.25,"Native fractional paragraph spacing");
  const auto spaced=state().layers;controller->undo();CHECK(state().layers==before_spacing,"Paragraph spacing undo");controller->redo();CHECK(state().layers==spaced,"Paragraph spacing redo");
  CHECK(controller->setPhotoLayerTypography(0,{{"family","Photo Editor Missing Font Fixture"}}),"Retain unavailable saved font");
  const auto missing=controller->photoLayerTextLayoutInfo(0);
  CHECK(missing.value("missingFont").toBool() && !missing.value("resolvedFamily").toString().isEmpty(),"Report missing font and resolved fallback");
  settle();CHECK(find("photoMissingFontWarning")->property("visible").toBool(),"Visible missing font warning");
  controller->undo();CHECK(state().layers==spaced,"Undo font substitution restores exact typography");
  CHECK(controller->setPhotoLayerLocked(0,true),"Lock typography");
  CHECK(!controller->setPhotoLayerTypography(0,{{"paragraphSpacing",3.0}}) && !controller->setPhotoLayerText(0,"Blocked"),"Locked text refuses edits");controller->undo();
  const auto final_state=state();
  const auto recipe=serialize_edit_recipe_json(final_state);
  CHECK(validate_edit_recipe_json(recipe).valid,"Version 2 typography validates in desktop recipe");
  QFile file(temp.filePath("typography.json"));CHECK(file.open(QIODevice::WriteOnly)&&file.write(recipe.toUtf8())>0,"Save document file");file.close();
  CHECK(controller->exportFlattenedPhoto(temp.filePath("export.png")),"Start actual text export");
  CHECK(wait_until([&]{return !controller->photoExportBusy();})&&controller->photoExportError().isEmpty(),"Text export completes");
  const QImage exported(temp.filePath("export.png"));CHECK(!exported.isNull(),"Text export loads");
  CHECK(file.open(QIODevice::ReadOnly),"Read saved document");
  auto reopened=std::make_unique<EditorPreviewController>();
  reopened->loadAsset("typography-reopened",source_path,{},{},{},{},QString::fromUtf8(file.readAll()));
  CHECK(EditorPreviewControllerTestAccess::state(*reopened).layers==final_state.layers,"New controller restores typography and point geometry");
  CHECK(reopened->exportFlattenedPhoto(temp.filePath("reopened.png")),"Reopened text export");
  CHECK(wait_until([&]{return !reopened->photoExportBusy();})&&reopened->photoExportError().isEmpty(),"Reopened export completes");
  CHECK(exported==QImage(temp.filePath("reopened.png")),"Text export must remain pixel-identical after disk reopen");
  if(!evidence.isEmpty()) {
    CHECK(exported.save(evidence+"/typography-export.png"),"Typography export evidence");
    CHECK(wait_until([&]{return !controller->processing()&&!controller->fullRenderRunning();}),"Final preview settles");
    settle();CHECK(window.grabWindow().save(evidence+"/paragraph-native.png"),"Paragraph screenshot");
  }
  std::cout << "Native point/paragraph creation, growing bounds, composition, Cancel, typography, font fallback, undo, disk reopen and identical PNG exports passed; photographic=" << photographic << '\n';
  return 0;
}
