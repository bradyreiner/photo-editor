#include "app_bootstrap.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "photo_render_utils.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QKeyEvent>
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
  static EditState state(const EditorPreviewController& c) { return c.currentState(); }
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
  QTemporaryDir temp;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",temp.path().toUtf8());
  CHECK(AppBootstrap::ensureRuntimeDirectories(),"Runtime directory");
  const auto photo_path=qEnvironmentVariable("PHOTO_EFFECT_TEST_SOURCE");
  QImage source=photo_path.isEmpty() ? QImage{} : QImage(photo_path);
  const bool photographic=!source.isNull();
  CHECK(photo_path.isEmpty() || photographic,"Requested photograph must load");
  if (photographic) source=source.scaled(640,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
  else {
    source=QImage(640,512,QImage::Format_RGB32);
    for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x)
      source.setPixelColor(x,y,QColor(x%256,y%256,(x/8+y/8)%2?220:40));
  }
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path=temp.filePath("source.png"); CHECK(source.save(source_path),"Source fixture");
  QQmlEngine engine;
  auto* provider=new EditorPreviewImageProvider;
  engine.addImageProvider("editor-preview",provider);
  auto controller=std::make_unique<EditorPreviewController>();
  controller->setImageProvider(provider);
  controller->loadAsset("effects-workflow",source_path,{},{},{},{},{});
  CHECK(wait_until([&]{return !controller->displayPreviewSource().isEmpty();}),"Source preview ready");
  CHECK(controller->addAdjustmentLayer("Photo effects"),"Adjustment target");
  engine.rootContext()->setContextProperty("testController",controller.get());
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components"
Rectangle {
    width: 980; height: 650; color: "#20242b"
    Image {
        objectName: "photoView"
        x: 330; y: 20; width: 630; height: 600
        fillMode: Image.PreserveAspectFit
        source: testController.displayPreviewSource
        cache: false
    }
    PhotoEffectStack {
        x: 15; y: 15; width: 295
        controller: testController
        layerIndex: 0
        layerData: testController.photoLayers.length ? testController.photoLayers[0] : null
    }
}
)QML",QUrl::fromLocalFile(QStringLiteral(PHOTO_EFFECT_TEST_ROOT "/tests/effect-workflow.qml")));
  std::unique_ptr<QObject> root(component.create());
  CHECK(root,component.errorString().toStdString());
  QQuickWindow window; window.resize(980,650);
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  window.show();window.requestActivate();
  CHECK(wait_until([&]{return window.isExposed() && !controller->processing() && !controller->fullRenderRunning();}),"Native window and source render ready");
  const auto settle=[] { QElapsedTimer timer;timer.start();while(timer.elapsed()<160) QCoreApplication::processEvents(QEventLoop::AllEvents,10); };
  settle();
  const auto find=[&](const QString& name) {
    QSet<QObject*> visited;
    std::function<QObject*(QObject*)> search=[&](QObject* object)->QObject* {
      if(!object || visited.contains(object))return nullptr;
      visited.insert(object);
      if(object->objectName()==name)return object;
      for(auto* child:object->children())if(auto* result=search(child))return result;
      if(auto* item=qobject_cast<QQuickItem*>(object))for(auto* child:item->childItems())if(auto* result=search(child))return result;
      return nullptr;
    };
    if(auto* result=search(root.get()))return result;
    return search(window.contentItem());
  };
  const auto click=[&](QObject* object) {
    auto* item=qobject_cast<QQuickItem*>(object);
    if(!item || !item->isVisible() || !item->isEnabled()) {
      std::cerr << "Unavailable native control " << (object ? object->objectName().toStdString() : "missing")
                << " visible=" << (item && item->isVisible()) << " enabled=" << (item && item->isEnabled()) << '\n';
      const auto dir=qEnvironmentVariable("PHOTO_EFFECT_EVIDENCE_DIR");
      if(!dir.isEmpty())window.grabWindow().save(dir+"/control-failure.png");
      return false;
    }
    const auto position=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    if(!QRectF(0,0,window.width(),window.height()).contains(position)) {
      std::cerr << "Control outside window " << object->objectName().toStdString() << " at " << position.x() << ',' << position.y() << '\n';
      return false;
    }
    const auto global=window.mapToGlobal(position.toPoint());
    QMouseEvent down(QEvent::MouseButtonPress,position,position,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QCoreApplication::sendEvent(&window,&down);
    QCoreApplication::processEvents();
    QMouseEvent up(QEvent::MouseButtonRelease,position,position,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(&window,&up);settle();return true;
  };
  const auto edit=[&](const QString& name,const QString& value) {
    auto* field=find(name);
    if(!click(field))return false;
    QMetaObject::invokeMethod(field,"selectAll");
    QKeyEvent type(QEvent::KeyPress,Qt::Key_unknown,Qt::NoModifier,value);
    QCoreApplication::sendEvent(&window,&type);
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QCoreApplication::sendEvent(&window,&enter);settle();return true;
  };
  const auto evidence=qEnvironmentVariable("PHOTO_EFFECT_EVIDENCE_DIR");
  if(!evidence.isEmpty()) { CHECK(QDir().mkpath(evidence),"Evidence directory");CHECK(source.save(evidence+"/source.png"),"Source evidence"); }
  const auto baseline=EditorPreviewControllerTestAccess::state(*controller).layers;
  const auto baseline_preview=controller->displayPreviewSource();
  CHECK(click(find("photoAddEffectButton")),"Open native effect picker");
  CHECK(click(find("photoEffectChoice_motion_blur")),"Choose motion blur using native pointer");
  CHECK(edit("photoEffectParameter_distance","37.25") && edit("photoEffectParameter_angle","-48.5"),"Native motion parameter input");
  auto motion=controller->photoLayerEffects(0).last().toMap();
  CHECK(motion.value("distance").toDouble()==37.25 && motion.value("angle").toDouble()==-48.5,"Fractional motion parameters reach controller");
  QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QCoreApplication::sendEvent(&window,&escape);settle();
  CHECK(EditorPreviewControllerTestAccess::state(*controller).layers==baseline,"Native Escape restores exact baseline");
  CHECK(click(find("photoAddEffectButton")) && click(find("photoEffectChoice_motion_blur")),"Reopen motion preview");
  CHECK(edit("photoEffectParameter_distance","24") && edit("photoEffectParameter_angle","25"),"Set accepted motion values");
  CHECK(click(find("photoEffectPreviewApply")),"Native Apply");
  const auto motion_layers=EditorPreviewControllerTestAccess::state(*controller).layers;
  CHECK(motion_layers!=baseline,"Apply retains effect");
  controller->undo();CHECK(EditorPreviewControllerTestAccess::state(*controller).layers==baseline,"Apply and all parameters use one undo");
  controller->redo();CHECK(EditorPreviewControllerTestAccess::state(*controller).layers==motion_layers,"Redo preserves effect identity");
  CHECK(wait_until([&]{return controller->displayPreviewSource()!=baseline_preview && !controller->processing() && !controller->fullRenderRunning();}),"Accepted motion preview publishes");
  if(!evidence.isEmpty()) { settle();CHECK(window.grabWindow().save(evidence+"/motion-native.png"),"Motion native screenshot"); }
  CHECK(click(find("photoAddEffectButton")) && click(find("photoEffectChoice_unsharp_mask")),"Native unsharp preview");
  CHECK(edit("photoEffectParameter_sharpenRadius","2.75") && edit("photoEffectParameter_strength","185") &&
      edit("photoEffectParameter_threshold","2.5"),"Native sharpening controls");
  const auto sharp=controller->photoLayerEffects(0).last().toMap();
  CHECK(sharp.value("sharpenRadius").toDouble()==2.75 && sharp.value("strength").toDouble()==1.85 &&
      sharp.value("threshold").toDouble()==0.025,"Sharpen controls preserve fractional settings");
  if(!evidence.isEmpty()) { settle();CHECK(window.grabWindow().save(evidence+"/unsharp-picker-native.png"),"Native unsharp picker screenshot"); }
  CHECK(click(find("photoEffectPreviewApply")),"Apply unsharp");
  const auto accepted=EditorPreviewControllerTestAccess::state(*controller).layers;
  const auto effect_id=sharp.value("id").toString();
  CHECK(controller->duplicatePhotoLayerEffect(0,effect_id),"Duplicate spatial effect");
  CHECK(controller->movePhotoLayerEffect(0,controller->photoLayerEffects(0).last().toMap().value("id").toString(),0),"Reorder spatial effect");
  controller->undo();controller->undo();CHECK(EditorPreviewControllerTestAccess::state(*controller).layers==accepted,"Undo reorder and duplicate");
  const auto state=EditorPreviewControllerTestAccess::state(*controller);
  QFile recipe(temp.filePath("recipe.json"));
  CHECK(recipe.open(QIODevice::WriteOnly) && recipe.write(serialize_edit_recipe_json(state).toUtf8())>0,"Persist recipe file");recipe.close();
  CHECK(controller->exportFlattenedPhoto(temp.filePath("export.png")),"Actual flattened PNG export");
  CHECK(wait_until([&]{return !controller->photoExportBusy();}) && controller->photoExportError().isEmpty(),"Export completes");
  const QImage exported(temp.filePath("export.png"));CHECK(!exported.isNull() && exported.size()==source.size(),"Export full fixture dimensions");
  CHECK(recipe.open(QIODevice::ReadOnly),"Reopen saved recipe");
  auto reopened=std::make_unique<EditorPreviewController>();
  reopened->loadAsset("effects-reopened",source_path,{},{},{},{},QString::fromUtf8(recipe.readAll()));
  CHECK(reopened->photoLayerEffects(0)==controller->photoLayerEffects(0),"New controller restores exact stack parameters");
  CHECK(reopened->exportFlattenedPhoto(temp.filePath("reopened.png")),"Export reopened document");
  CHECK(wait_until([&]{return !reopened->photoExportBusy();}) && reopened->photoExportError().isEmpty(),"Reopened export completes");
  CHECK(exported==QImage(temp.filePath("reopened.png")),"Export pixels must survive disk save and controller reopen exactly");
  auto clean=state;clean.layers.clear();
  const auto plain=render_output_image_with_backend(source,clean,QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64,RenderBackendPreference::CpuOnly);
  CHECK(!plain.isNull() && plain.convertToFormat(QImage::Format_RGBA8888)!=exported.convertToFormat(QImage::Format_RGBA8888),"Effects must visibly change exported pixels");
  if(!evidence.isEmpty()) {
    CHECK(exported.save(evidence+"/effects-export.png"),"Effect export evidence");
    CHECK(plain.save(evidence+"/baseline-export.png"),"Baseline export evidence");
    settle();CHECK(window.grabWindow().save(evidence+"/stack-native.png"),"Native stack screenshot");
  }
  std::cout << "Native pointer/key preview, Cancel/Apply, fractional controls, undo/redo, duplicate/reorder, disk reopen and exact PNG export passed; photographic=" << photographic << '\n';
  return 0;
}
