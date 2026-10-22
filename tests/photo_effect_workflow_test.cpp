#include "app_bootstrap.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "photo_render_utils.h"
#include "photo_effect_looks.h"
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
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
  if (photographic) {
    if (!qEnvironmentVariableIsSet("PHOTO_EFFECT_TEST_PRESERVE_RESOLUTION"))
      source=source.scaled(640,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
  } else {
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
  EditRecipeState initial_state;
  const auto review_without_mask=qEnvironmentVariableIsSet("PHOTO_EFFECT_TEST_NO_MASK");
  if(!review_without_mask) {
    QByteArray subject_alpha(source.width()*source.height(),char(0));
    for(int y=70;y<source.height()-70;++y)
      for(int x=source.width()/3;x<(source.width()*2)/3;++x)
        subject_alpha[y*source.width()+x]=char(255);
    initial_state.mask_stack={QVariantMap{{"id","subject-component"},{"maskSetId","subject-selection"},
        {"maskSetName","Subject"},{"toolRole","selection"},{"kind","brush"},
        {"enabled",true},{"operation","add"},{"segmentationWidth",source.width()},
        {"segmentationHeight",source.height()},{"segmentationData",subject_alpha}}};
    initial_state.active_mask_index=0;
  }
  controller->loadAsset("effects-workflow",source_path,{},{},{},{},serialize_edit_recipe_json(initial_state));
  CHECK(wait_until([&]{return !controller->displayPreviewSource().isEmpty();}),"Source preview ready");
  CHECK(review_without_mask || controller->selectMaskSet(QStringLiteral("subject-selection")),
      "Select the subject mask for the effect workflow");
  CHECK(controller->addAdjustmentLayer("Photo effects"),"Adjustment target");
  const auto effects_before_look = controller->photoLayerEffects(0).size();
  CHECK(controller->applyPhotoLayerEffectLook(0, QStringLiteral("dream_haze")),
      "One-click look appends its stages");
  CHECK(controller->photoLayerEffects(0).size() == effects_before_look + 2,
      "One-click look appends both editable stages");
  controller->undo();
  CHECK(controller->photoLayerEffects(0).size() == effects_before_look,
      "One-click look is one undo operation");
  controller->redo();
  CHECK(controller->photoLayerEffects(0).size() == effects_before_look + 2,
      "One-click look is restored by one redo operation");
  controller->undo();
  CHECK(controller->setPhotoLayerLocked(0, true), "Lock effect target");
  CHECK(!controller->applyPhotoLayerEffectLook(0, QStringLiteral("dream_haze")) &&
      controller->photoLayerEffects(0).size() == effects_before_look,
      "Locked targets reject looks without appending effects");
  CHECK(controller->setPhotoLayerLocked(0, false), "Unlock effect target");
  engine.rootContext()->setContextProperty("testController",controller.get());
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQuick.Controls.Basic
import "../apps/desktop/qml/components"
Rectangle {
    property bool showTextMaterials: false
    width: 980; height: 650; color: "#20242b"
    Image {
        objectName: "photoView"
        x: 330; y: 20; width: 630; height: 600
        fillMode: Image.PreserveAspectFit
        source: testController.displayPreviewSource
        cache: false
    }
    PhotoEffectStack {
        objectName: "testFilterStack"
        visible: !parent.showTextMaterials
        x: 15; y: 15; width: 295
        controller: testController
        layerIndex: 0
        layerData: testController.photoLayers.length ? testController.photoLayers[0] : null
    }
    Loader {
        active: parent.showTextMaterials
        x: 15; y: 15; width: 295; height: 620
        sourceComponent: ScrollView {
            clip: true
            contentWidth: availableWidth
            PhotoTypographyPanel {
                width: 295
                controller: testController
                layerIndex: 0
                layerData: testController.photoLayers.length ? testController.photoLayers[0] : ({})
                effectsExpanded: true
            }
        }
    }
}
)QML",QUrl::fromLocalFile(QStringLiteral(PHOTO_EFFECT_TEST_ROOT "/tests/effect-workflow.qml")));
  std::unique_ptr<QObject> root(component.create());
  CHECK(root,component.errorString().toStdString());
  QQuickWindow window; window.setFlags(Qt::Window | Qt::FramelessWindowHint); window.resize(980,650);
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  window.show();window.requestActivate();
  CHECK(wait_until([&]{return window.isExposed() && !controller->processing() && !controller->fullRenderRunning();}),"Offscreen Qt Quick window and source render ready");
  const auto settle=[] { QElapsedTimer timer;timer.start();while(timer.elapsed()<160) QCoreApplication::processEvents(QEventLoop::AllEvents,10); };
  settle();
  const auto find=[&](const QString& name) {
    QSet<QObject*> visited;
    QObject* hidden_match=nullptr;
    std::function<QObject*(QObject*)> search=[&](QObject* object)->QObject* {
      if(!object || visited.contains(object))return nullptr;
      visited.insert(object);
      if(object->objectName()==name) {
        auto* item=qobject_cast<QQuickItem*>(object);
        if((item && item->isVisible()) || (!item && object->property("visible").toBool()))return object;
        if(!hidden_match)hidden_match=object;
      }
      for(auto* child:object->children())if(auto* result=search(child))return result;
      if(auto* item=qobject_cast<QQuickItem*>(object))for(auto* child:item->childItems())if(auto* result=search(child))return result;
      return nullptr;
    };
    if(auto* result=search(root.get()))return result;
    if(auto* result=search(window.contentItem()))return result;
    return hidden_match;
  };
  const auto click=[&](QObject* object) {
    auto* item=qobject_cast<QQuickItem*>(object);
    if(!item || !item->isVisible() || !item->isEnabled()) {
      std::cerr << "Unavailable Qt Quick control " << (object ? object->objectName().toStdString() : "missing")
                << " visible=" << (item && item->isVisible()) << " enabled=" << (item && item->isEnabled()) << '\n';
      const auto dir=qEnvironmentVariable("PHOTO_EFFECT_EVIDENCE_DIR");
      if(!dir.isEmpty())window.grabWindow().save(dir+"/control-failure.png");
      return false;
    }
    // Follow the real nested scroll surfaces before delivering a pointer event.
    // Popup actions stay fixed while its long discovery/settings body scrolls.
    for (int pass=0;pass<2;++pass) {
      for (auto* ancestor=item->parentItem();ancestor;ancestor=ancestor->parentItem()) {
        if (!ancestor->property("contentY").isValid() || !ancestor->property("contentHeight").isValid()) continue;
        const auto point=item->mapToItem(ancestor,QPointF(0,item->height()/2));
        const auto maximum=std::max(0.0,ancestor->property("contentHeight").toDouble()-ancestor->height());
        const auto next=std::clamp(ancestor->property("contentY").toDouble()+point.y()-ancestor->height()/2,0.0,maximum);
        ancestor->setProperty("contentY",next);settle();
      }
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
    auto* field=find(name+"ValueField");
    if(!field) std::cerr << "Missing numeric test field " << name.toStdString() << '\n';
    auto* field_item=qobject_cast<QQuickItem*>(field);
    auto* settings_scroll=qobject_cast<QQuickItem*>(find("photoEffectSettingsScrollView"));
    if(field_item && settings_scroll) {
      QSet<QObject*> visited;
      std::function<QObject*(QObject*)> find_flickable=[&](QObject* object)->QObject* {
        if(!object || visited.contains(object))return nullptr;
        visited.insert(object);
        if(object->property("contentY").isValid())return object;
        for(auto* child:object->children())if(auto* result=find_flickable(child))return result;
        if(auto* item=qobject_cast<QQuickItem*>(object))for(auto* child:item->childItems())if(auto* result=find_flickable(child))return result;
        return nullptr;
      };
      if(auto* flickable=find_flickable(settings_scroll)) {
        if(auto* flickable_item=qobject_cast<QQuickItem*>(flickable)) {
          const auto point=field_item->mapToItem(flickable_item,QPointF(0,field_item->height()/2));
          const auto next_y=std::max(0.0,flickable->property("contentY").toDouble()+point.y()-flickable_item->height()/2);
          flickable->setProperty("contentY",next_y);
          settle();
        }
      }
    }
    if(!click(field))return false;
    QMetaObject::invokeMethod(field,"selectAll");
    QKeyEvent type(QEvent::KeyPress,Qt::Key_unknown,Qt::NoModifier,value);
    QCoreApplication::sendEvent(&window,&type);
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QCoreApplication::sendEvent(&window,&enter);settle();return true;
  };
  const auto scroll_into_view=[&](QObject* object) {
    auto* target=qobject_cast<QQuickItem*>(object);
    auto* scroll=qobject_cast<QQuickItem*>(find("photoEffectSettingsScrollView"));
    if(!target || !scroll)return false;
    QSet<QObject*> visited;
    std::function<QObject*(QObject*)> find_flickable=[&](QObject* node)->QObject* {
      if(!node || visited.contains(node))return nullptr;
      visited.insert(node);
      if(node->property("contentY").isValid())return node;
      for(auto* child:node->children())if(auto* result=find_flickable(child))return result;
      if(auto* item=qobject_cast<QQuickItem*>(node))for(auto* child:item->childItems())if(auto* result=find_flickable(child))return result;
      return nullptr;
    };
    auto* flickable=find_flickable(scroll);
    auto* view=qobject_cast<QQuickItem*>(flickable);
    if(!flickable || !view)return false;
    const auto point=target->mapToItem(view,QPointF(0,target->height()));
    const auto max_y=std::max(0.0,flickable->property("contentHeight").toDouble()-view->height());
    const auto next_y=std::clamp(flickable->property("contentY").toDouble()+point.y()-view->height(),0.0,max_y);
    flickable->setProperty("contentY",next_y);settle();return true;
  };
  const auto evidence=qEnvironmentVariable("PHOTO_EFFECT_EVIDENCE_DIR");
  if(!evidence.isEmpty()) { CHECK(QDir().mkpath(evidence),"Evidence directory");CHECK(source.save(evidence+"/source.png"),"Source evidence"); }
#include "film_grain_workflow_proof.inc"
  if(qEnvironmentVariableIsSet("PHOTO_EFFECT_EXTENDED_REVIEW_ONLY")) {
    CHECK(controller->availablePhotoEffects().size() == 35, "All 35 operators are exposed");
    CHECK(controller->exportFlattenedPhoto(temp.filePath("extended-baseline.png")), "Export reference");
    CHECK(wait_until([&]{return !controller->photoExportBusy();}) && controller->photoExportError().isEmpty(), "Reference export");
    const QImage baseline(temp.filePath("extended-baseline.png"));
    const QStringList types{"halation", "anamorphic_streaks", "starburst", "lens_bokeh", "tilt_shift",
        "zoom_blur", "spin_blur", "surface_blur", "high_pass", "gradient_map", "solarization",
        "color_halftone", "dither", "crosshatch", "ink_outline", "watercolor", "oil_paint",
        "pixel_mosaic", "rgb_glitch", "displacement_map"};
    QImage map(32,32,QImage::Format_RGBA8888);
    map.fill(QColor(220,95,128,255));
    const auto map_path=temp.filePath("vector-map.png"); CHECK(map.save(map_path), "Data map");
    for(const auto& type:types) {
      if(qEnvironmentVariableIsSet("PHOTO_EFFECT_TEXT_REFERENCE_ONLY")) continue;
      CHECK(controller->photoLayerEffects(0).isEmpty(), "Each operator starts isolated");
      CHECK(click(find("photoAddEffectButton")), "Open grouped discovery");
      auto* picker=find("photoEffectPicker");
      CHECK(picker && !picker->property("modal").toBool() && !picker->property("dim").toBool(), "Preview never dims canvas");
      CHECK(wait_until([&]{return picker->property("opened").toBool();}), "Discovery popup finishes opening");
      CHECK(click(find("photoEffectChoice_"+type)), "Native pointer chooses new filter");
      auto rows=controller->photoLayerEffects(0);
      if(rows.size()!=1 || !rows[0].toMap().value("extended").toBool()) {
        std::cerr << "Preview selection " << type.toStdString() << " rows=" << rows.size()
                  << " visible=" << picker->property("visible").toBool()
                  << " browsing=" << picker->property("browsing").toBool()
                  << " preview=" << picker->property("previewType").toString().toStdString()
                  << " error=" << picker->property("errorMessage").toString().toStdString() << '\n';
        if(!evidence.isEmpty())window.grabWindow().save(evidence+"/control-failure.png");
      }
      CHECK(rows.size()==1 && rows[0].toMap().value("extended").toBool(), "New filter preview metadata");
      auto parameters=rows[0].toMap(); const auto id=parameters.value("id").toString();
      CHECK(picker->property("previewId").toString()==id, "Picker tracks editable preview");
      if(type=="halation" || type=="anamorphic_streaks" || type=="starburst" || type=="lens_bokeh") {
        parameters.insert("threshold",0.2);
        CHECK(controller->setPhotoLayerEffectParameters(0,type,id,parameters,1,true), "Tune highlight extraction");
      }
      if(type=="gradient_map") {
        parameters.insert("gradientStops",QVariantList{QVariantMap{{"position",0.0},{"color","#142a5c"}},
            QVariantMap{{"position",0.45},{"color","#bd6341"}},QVariantMap{{"position",1.0},{"color","#ffedbf"}}});
        CHECK(controller->setPhotoLayerEffectParameters(0,type,id,parameters,1,true), "Color-managed multi-stop gradient");
      }
      if(type=="displacement_map") CHECK(controller->setPhotoLayerDisplacementMap(0,id,map_path), "Import managed vector map");
      CHECK(QMetaObject::invokeMethod(find("testFilterStack"),"refreshEffectPreviewParameters"), "Refresh settings after controller edits");
      if(type=="gradient_map") {
        CHECK(find("photoGradientMapStopColor_0") && find("photoGradientMapStopColor_2"), "All nested gradient stop controls instantiate");
        CHECK(click(find("photoGradientMapAddStop")), "Native pointer inserts a color-managed stop");
        CHECK(controller->photoLayerEffects(0)[0].toMap().value("gradientStops").toList().size()==4, "Added stop is editable");
        CHECK(click(find("photoGradientMapRemoveStop_1")), "Native pointer removes a gradient stop");
        CHECK(controller->photoLayerEffects(0)[0].toMap().value("gradientStops").toList().size()==3, "Removed stop restores count");
        parameters=controller->photoLayerEffects(0)[0].toMap();
      }
      if(type=="dither") {
        auto* mode=qobject_cast<QQuickItem*>(find("photoExtendedEffectChoice_mode"));
        CHECK(mode && mode->isVisible(), "Nested enum choices render as labeled picker");
        CHECK(click(mode), "Open native dither mode picker");
        CHECK(click(find("photoExtendedEffectChoice_modeOption1")), "Native pointer chooses error diffusion");
        CHECK(controller->photoLayerEffects(0)[0].toMap().value("mode").toInt()==1, "Native enum changes to Floyd-Steinberg");
        const auto* matrix=find("photoExtendedEffectChoice_matrix_size");
        CHECK(matrix && !matrix->property("visible").toBool(), "Bayer-only control hides for error diffusion");
        parameters=controller->photoLayerEffects(0)[0].toMap();
      }
      const auto fields=controller->photoEffectParameterFields(type);
      if(!fields.isEmpty()) CHECK(find("photoExtendedEffectParameter_"+fields[0].toMap().value("key").toString()), "New settings instantiate");
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}), "Filter renders");
      if(!evidence.isEmpty()) { settle(); CHECK(window.grabWindow().save(evidence+"/"+type+"-native.png"), "Native settings screenshot"); }
      CHECK(click(find("photoEffectPreviewCancel")), "Cancel native preview");
      CHECK(wait_until([&]{return controller->photoLayerEffects(0).isEmpty();}), "Cancel restores entire layer including assets");
      CHECK(controller->previewPhotoLayerEffect(0,type), "Start approved effect");
      parameters.remove("id");
      const auto commit_id=controller->photoLayerEffects(0)[0].toMap().value("id").toString();
      if(type!="displacement_map") controller->setPhotoLayerEffectParameters(0,type,commit_id,parameters,1,true);
      else CHECK(controller->setPhotoLayerDisplacementMap(0,commit_id,map_path), "Bind committed map");
      CHECK(controller->finishPhotoLayerEffectPreview(true), "Apply one transaction");
      const auto expected=controller->photoLayerEffects(0);
      controller->undo(); CHECK(controller->photoLayerEffects(0).isEmpty(), "One undo removes effect and imported map");
      controller->redo(); CHECK(controller->photoLayerEffects(0)==expected, "Redo exact settings");
      CHECK(controller->exportFlattenedPhoto(temp.filePath(type+".png")), "Export operator");
      CHECK(wait_until([&]{return !controller->photoExportBusy();}) && controller->photoExportError().isEmpty(), "Operator export complete");
      const QImage rendered(temp.filePath(type+".png"));
      CHECK(!rendered.isNull() && rendered!=baseline, "Operator visibly changes reference image");
      const auto state=EditorPreviewControllerTestAccess::state(*controller);
      const auto recipe=serialize_edit_recipe_json(state);
      if(type=="displacement_map") CHECK(QFile::remove(map_path), "Remove original map before reopening managed project");
      auto reopened=std::make_unique<EditorPreviewController>();
      reopened->loadAsset("extended-reopened",source_path,{},{},{},{},recipe);
      CHECK(reopened->photoLayerEffects(0)==expected, "Recipe round-trip preserves structured operator settings");
      CHECK(reopened->exportFlattenedPhoto(temp.filePath("reopened-"+type+".png")), "Export reopened operator");
      CHECK(wait_until([&]{return !reopened->photoExportBusy();}) && reopened->photoExportError().isEmpty(), "Reopened export completes");
      CHECK(rendered==QImage(temp.filePath("reopened-"+type+".png")), "Reopened export is pixel identical");
      if(!evidence.isEmpty()) {
        CHECK(rendered.save(evidence+"/"+type+".png"), "Photographic output");
        QFile saved(evidence+"/"+type+".json"); CHECK(saved.open(QIODevice::WriteOnly) && saved.write(recipe.toUtf8())>0, "Editable recipe artifact");
      }
      controller->undo(); CHECK(controller->photoLayerEffects(0).isEmpty(), "Prepare next isolated operator");
      std::cout << "Verified extended operator " << type.toStdString() << '\n';
    }
    QFile::remove(map_path);
    CHECK(controller->addPointTextLayer("LOOKSY",source.width()/8,source.height()/2,source.width(),source.height()), "Editable text source");
    root->setProperty("showTextMaterials",true);
    const QStringList materials{"frosted_glass","smoked_plastic","soft_emboss","polished_chrome","brushed_silver",
        "warm_gold","copper","dark_steel","champagne_gold","rose_gold","antique_brass","titanium","black_chrome"};
    for(const auto& material:materials) {
      QVariantMap style{{"family","Helvetica"},{"size",source.width()/9.0},{"weight",800},{"color","#ffffff"},
          {"material",material},{"materialTint","#ffdbd9cf"},{"materialDepth",6},{"materialEdgeSoftness",1},
          {"materialBlur",material=="frosted_glass"?6:0},{"materialTransmission",.78},{"materialLightAngle",-45}};
      CHECK(controller->setPhotoLayerTypography(0,style), "Set editable material");
      const auto expected=controller->photoLayerTypography(0);
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}), "Text material preview");
      if(!evidence.isEmpty()) {settle();CHECK(window.grabWindow().save(evidence+"/text-"+material+"-native.png"),"Native text material frame");}
      controller->undo();controller->redo();CHECK(controller->photoLayerTypography(0)==expected,"Text material undo/redo exact");
      CHECK(controller->exportFlattenedPhoto(temp.filePath("text-"+material+".png")),"Export material text");
      CHECK(wait_until([&]{return !controller->photoExportBusy();}) && controller->photoExportError().isEmpty(),"Material export");
      const QImage rendered(temp.filePath("text-"+material+".png"));CHECK(!rendered.isNull()&&rendered!=baseline,"Material visible over photograph");
      const auto recipe=serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(*controller));
      auto reopened=std::make_unique<EditorPreviewController>();reopened->loadAsset("text-material-reopened",source_path,{},{},{},{},recipe);
      CHECK(reopened->photoLayerTypography(0)==expected,"Editable material survives recipe reopen");
      CHECK(reopened->exportFlattenedPhoto(temp.filePath("text-reopened.png")),"Export reopened text material");
      CHECK(wait_until([&]{return !reopened->photoExportBusy();})&&reopened->photoExportError().isEmpty(),"Reopened material export");
      CHECK(rendered==QImage(temp.filePath("text-reopened.png")),"Material export exact after reopen");
      if(!evidence.isEmpty()) {
        CHECK(rendered.save(evidence+"/text-"+material+".png"),"Material photograph artifact");
        QFile saved(evidence+"/text-"+material+".json");CHECK(saved.open(QIODevice::WriteOnly)&&saved.write(recipe.toUtf8())>0,"Editable text artifact");
      }
      std::cout<<"Verified editable text material "<<material.toStdString()<<'\n';
    }
    std::cout << "Twenty native filter previews, cancellation, undo/redo, recipe reopen and exact exports passed; photographic=" << photographic << '\n';
    return 0;
  }
  if(qEnvironmentVariableIsSet("PHOTO_EFFECT_CREATIVE_REVIEW_ONLY")) {
    CHECK(controller->photoLayerEffects(0).isEmpty(), "Creative review starts with an empty stack");
    const auto clean_state=EditorPreviewControllerTestAccess::state(*controller);
    const auto baseline=render_output_image_with_backend(source,clean_state,
        QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64,RenderBackendPreference::CpuOnly);
    CHECK(!baseline.isNull(), "Creative review baseline renders");
    if(!evidence.isEmpty()) CHECK(baseline.save(evidence+"/baseline.png"), "Save creative baseline");
    for(std::size_t look_index=36;look_index<photo_effect_looks.size();++look_index) {
      const auto& look=photo_effect_looks[look_index];
      const auto id=QString::fromUtf8(look.id.data(),static_cast<qsizetype>(look.id.size()));
      const auto name=QString::fromUtf8(look.name.data(),static_cast<qsizetype>(look.name.size()));
      CHECK(click(find("photoAddEffectButton")), "Open creative look picker");
      auto* search=find("photoEffectSearchField");
      CHECK(search, "Creative look search is available");
      search->setProperty("text",name);settle();
      CHECK(click(find("photoEffectLookChoice_"+id)), "Apply curated look through its native button");
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}),
          "Creative look preview settles");
      const auto expected=controller->photoLayerEffects(0);
      CHECK(expected.size()==static_cast<qsizetype>(look.stage_count), "Curated look remains separate editable stages");
      const auto state=EditorPreviewControllerTestAccess::state(*controller);
      const auto rendered=render_output_image_with_backend(source,state,
          QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64,RenderBackendPreference::CpuOnly);
      CHECK(!rendered.isNull() && rendered!=baseline, "Curated look changes the actual image");
      controller->undo();
      CHECK(controller->photoLayerEffects(0).isEmpty(), "Curated look has one undo unit");
      controller->redo();
      CHECK(controller->photoLayerEffects(0)==expected, "Redo restores exact effect identities and values");
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}),
          "Redo preview settles before capture");
      const auto recipe_json=serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(*controller));
      CHECK(controller->exportFlattenedPhoto(temp.filePath("creative-export.png")), "Export creative look");
      CHECK(wait_until([&]{return !controller->photoExportBusy();}) && controller->photoExportError().isEmpty(),
          "Creative look PNG export completes");
      const QImage exported(temp.filePath("creative-export.png"));
      CHECK(!exported.isNull() && exported.size()==source.size(), "Creative export retains source dimensions");
      auto reopened=std::make_unique<EditorPreviewController>();
      reopened->loadAsset("creative-reopened",source_path,{},{},{},{},recipe_json);
      CHECK(reopened->photoLayerEffects(0)==expected, "Reopen preserves every curated effect parameter");
      CHECK(reopened->exportFlattenedPhoto(temp.filePath("creative-reopened.png")), "Export reopened creative look");
      CHECK(wait_until([&]{return !reopened->photoExportBusy();}) && reopened->photoExportError().isEmpty(),
          "Reopened creative export completes");
      CHECK(exported==QImage(temp.filePath("creative-reopened.png")), "Creative export is pixel-identical after reopen");
      if(!evidence.isEmpty()) {
        CHECK(exported.save(evidence+"/"+id+".png"), "Save creative look render");
        QFile saved_recipe(evidence+"/"+id+".json");
        CHECK(saved_recipe.open(QIODevice::WriteOnly) && saved_recipe.write(recipe_json.toUtf8())>0,
            "Save editable creative review recipe");
        settle();
        CHECK(window.grabWindow().save(evidence+"/"+id+"-native.png"), "Save native creative look screenshot");
      }
      controller->undo();
      CHECK(controller->photoLayerEffects(0).isEmpty(), "Next curated look starts clean");
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}),
          "Clean preview settles");
    }
    struct CreativeControl { const char* type; const char* name; const char* key; const char* input; double expected; };
    const std::array controls{
        CreativeControl{"film_grain", "Film grain", "grainStrength", "3.25", .0325},
        CreativeControl{"light_leak", "Light leak", "leakCenterX", "-12.5", -.125},
        CreativeControl{"split_tone", "Split tone", "shadowHue", "190", 190},
        CreativeControl{"vignette", "Vignette", "vignetteStrength", "45", .45},
        CreativeControl{"duotone", "Duotone", "duotoneStrength", "67.5", .675},
        CreativeControl{"soft_fade", "Soft fade", "lift", "2.5", .025}};
    for(const auto& control:controls) {
      CHECK(click(find("photoAddEffectButton")), "Open new operator picker");
      auto* search=find("photoEffectSearchField");CHECK(search, "New operator search exists");
      search->setProperty("text",QString::fromUtf8(control.name));settle();
      CHECK(click(find("photoEffectChoice_"+QString::fromUtf8(control.type))), "Choose real new operator");
      CHECK(edit("photoEffectParameter_"+QString::fromUtf8(control.key),QString::fromUtf8(control.input)),
          "Edit a new operator using its numeric control");
      const auto draft=controller->photoLayerEffects(0);
      CHECK(draft.size()==1 && draft.first().toMap().value("type").toString()==QString::fromUtf8(control.type) &&
          std::abs(draft.first().toMap().value(QString::fromUtf8(control.key)).toDouble()-control.expected)<1e-8,
          "New operator control publishes exact typed parameters");
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}),
          "New operator preview settles");
      if(!evidence.isEmpty()) {
        settle();CHECK(window.grabWindow().save(evidence+"/operator-"+QString::fromUtf8(control.type)+"-native.png"),
            "Capture actual new operator control preview");
      }
      CHECK(click(find("photoEffectPreviewApply")), "Accept new operator preview");
      controller->undo();CHECK(controller->photoLayerEffects(0).isEmpty(), "New operator and control edit undo together");
      controller->redo();CHECK(controller->photoLayerEffects(0)==draft, "New operator redo restores edited value");
      controller->undo();
      CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}), "New operator undo settles");
      CHECK(click(find("photoAddEffectButton")), "Open cancellation check");
      search=find("photoEffectSearchField");search->setProperty("text",QString::fromUtf8(control.name));settle();
      CHECK(click(find("photoEffectChoice_"+QString::fromUtf8(control.type))), "Preview new operator for cancellation");
      CHECK(click(find("photoEffectPreviewCancel")) && controller->photoLayerEffects(0).isEmpty(),
          "Cancel discards new operator preview");
    }
    std::cout << "Creative looks: native picker, separate editable stages, one-step undo/redo, recipe reopen and exact PNG export passed; photographic="
              << photographic << '\n';
    return 0;
  }
  if(qEnvironmentVariableIsSet("PHOTO_EFFECT_REVIEW_ONLY")) {
    CHECK(click(find("photoAddEffectButton")),"Open the F1 review effect picker");
    auto* search=find("photoEffectSearchField");
    CHECK(search,"F1 review filter search exists");
    search->setProperty("text",QStringLiteral("optical"));
    settle();
    const auto previous_preview=controller->displayPreviewSource();
    CHECK(click(find("photoEffectChoice_optical_diffusion")),"Choose Optical Diffusion for F1 review");
    const auto parameter=[](const char* name,double fallback) {
      bool ok=false;
      const auto value=qEnvironmentVariable(name).toDouble(&ok);
      return ok && std::isfinite(value) ? value : fallback;
    };
    const auto radius=parameter("PHOTO_EFFECT_REVIEW_RADIUS",31.5);
    const auto strength=parameter("PHOTO_EFFECT_REVIEW_STRENGTH",140.0);
    const auto warmth=parameter("PHOTO_EFFECT_REVIEW_WARMTH",30.0);
    const auto protection=parameter("PHOTO_EFFECT_REVIEW_PROTECTION",65.0);
    const auto threshold=parameter("PHOTO_EFFECT_REVIEW_THRESHOLD",0.8);
    CHECK(edit("photoEffectParameter_diffusionRadius",QString::number(radius,'g',16)) &&
        edit("photoEffectParameter_highlightThreshold",QString::number(threshold,'g',16)) &&
        edit("photoEffectParameter_highlightKnee","0.25") &&
        edit("photoEffectParameter_diffusionStrength",QString::number(strength,'g',16)) &&
        edit("photoEffectParameter_warmth",QString::number(warmth,'g',16)) &&
        edit("photoEffectParameter_highlightProtection",QString::number(protection,'g',16)),
        "Set all six Optical Diffusion controls through the Qt Quick review workflow");
    const auto effect=controller->photoLayerEffects(0).last().toMap();
    CHECK(effect.value("type").toString()==QStringLiteral("optical_diffusion") &&
        std::abs(effect.value("diffusionRadius").toDouble()-radius)<1e-6 &&
        std::abs(effect.value("highlightThreshold").toDouble()-threshold)<1e-6 &&
        std::abs(effect.value("diffusionStrength").toDouble()-strength/100.0)<1e-6 &&
        std::abs(effect.value("warmth").toDouble()-warmth/100.0)<1e-6 &&
        std::abs(effect.value("highlightProtection").toDouble()-protection/100.0)<1e-6,
        "F1 review parameters reach the real photo-effect controller");
    if(!qEnvironmentVariableIsSet("PHOTO_EFFECT_TEST_PRESERVE_RESOLUTION")) {
      CHECK(wait_until([&]{return controller->displayPreviewSource()!=previous_preview &&
          !controller->processing() && !controller->fullRenderRunning();}),
          "F1 preview settles before the native-control screenshot");
    }
    if(!evidence.isEmpty() && !qEnvironmentVariableIsSet("PHOTO_EFFECT_TEST_PRESERVE_RESOLUTION")) {
      settle();
      CHECK(window.grabWindow().save(evidence+"/diffusion-qt-offscreen.png"),
          "Save the settled F1 native-control screenshot");
    }
    CHECK(click(find("photoEffectPreviewApply")),"Apply Optical Diffusion through its native control");
    CHECK(wait_until([&]{return !controller->processing() && !controller->fullRenderRunning();}),
        "F1 apply settles before rendering the review output");
    const auto state=EditorPreviewControllerTestAccess::state(*controller);
    auto baseline_state=state;
    baseline_state.layers.clear();
    const auto rendered=render_output_image_with_backend(
        source,state,QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64,
        RenderBackendPreference::CpuOnly);
    const auto baseline_image=render_output_image_with_backend(
        source,baseline_state,QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64,
        RenderBackendPreference::CpuOnly);
    CHECK(!rendered.isNull() && !baseline_image.isNull() && rendered.size()==source.size() &&
        baseline_image.size()==source.size() && rendered!=baseline_image,
        "Optical Diffusion changes the complete real-photo CPU render");
    std::uint64_t changed_pixels=0;
    std::uint64_t channel_samples=0;
    std::uint64_t absolute_delta_sum=0;
    int maximum_channel_delta=0;
    QImage difference(rendered.size(),QImage::Format_RGBA8888);
    for(int y=0;y<rendered.height();++y) for(int x=0;x<rendered.width();++x) {
      const auto before=baseline_image.pixelColor(x,y);
      const auto after=rendered.pixelColor(x,y);
      const auto red_delta=std::abs(before.red()-after.red());
      const auto green_delta=std::abs(before.green()-after.green());
      const auto blue_delta=std::abs(before.blue()-after.blue());
      const auto pixel_max=std::max({red_delta,green_delta,blue_delta});
      maximum_channel_delta=std::max(maximum_channel_delta,pixel_max);
      absolute_delta_sum+=static_cast<std::uint64_t>(red_delta+green_delta+blue_delta);
      channel_samples+=3;
      if(pixel_max>0) ++changed_pixels;
      difference.setPixelColor(x,y,QColor(
          std::min(255,red_delta*8),std::min(255,green_delta*8),
          std::min(255,blue_delta*8),255));
    }
    const auto mean_absolute_delta=static_cast<double>(absolute_delta_sum)/
        static_cast<double>(channel_samples);
    const auto changed_pixel_fraction=static_cast<double>(changed_pixels)/
        static_cast<double>(rendered.width()*rendered.height());
    if(!evidence.isEmpty()) {
      CHECK(rendered.save(evidence+"/diffusion-only-export.png"),"Save the isolated F1 output");
      CHECK(baseline_image.save(evidence+"/diffusion-only-baseline.png"),"Save the isolated F1 baseline");
      CHECK(difference.save(evidence+"/diffusion-only-difference.png"),"Save the amplified F1 difference image");
      QFile metrics(evidence+"/diffusion-only-metrics.json");
      CHECK(metrics.open(QIODevice::WriteOnly),"Open F1 render metrics file");
      const auto json=QStringLiteral(
          "{\"meanAbsoluteChannelDelta8Bit\":%1,\"maximumChannelDelta8Bit\":%2,\"changedPixelFraction\":%3,\"changedPixels\":%4,\"width\":%5,\"height\":%6}\n")
          .arg(mean_absolute_delta,0,'g',12).arg(maximum_channel_delta)
          .arg(changed_pixel_fraction,0,'g',12).arg(changed_pixels)
          .arg(rendered.width()).arg(rendered.height());
      CHECK(metrics.write(json.toUtf8())==json.size(),"Write F1 render metrics");
      QFile recipe(evidence+"/saved-recipe.json");
      CHECK(recipe.open(QIODevice::WriteOnly) &&
          recipe.write(serialize_edit_recipe_json(state).toUtf8())>0,
          "Save the editable F1 recipe");
    }
    CHECK(controller->exportFlattenedPhoto(temp.filePath("f1-export.png")),
        "Export the F1 document through the normal photo exporter");
    CHECK(wait_until([&]{return !controller->photoExportBusy();}) &&
        controller->photoExportError().isEmpty(),"F1 flattened export completes");
    const QImage exported(temp.filePath("f1-export.png"));
    CHECK(exported.size()==source.size() && !exported.isNull(),"F1 export retains source dimensions");
    auto reopened=std::make_unique<EditorPreviewController>();
    reopened->loadAsset("f1-review-reopened",source_path,{},{},{},{},
        serialize_edit_recipe_json(state));
    CHECK(reopened->photoLayerEffects(0)==controller->photoLayerEffects(0),
        "F1 recipe reopens with identical editable effect parameters");
    CHECK(reopened->exportFlattenedPhoto(temp.filePath("f1-reopened-export.png")),
        "Export the reopened F1 document");
    CHECK(wait_until([&]{return !reopened->photoExportBusy();}) &&
        reopened->photoExportError().isEmpty(),"Reopened F1 export completes");
    CHECK(exported==QImage(temp.filePath("f1-reopened-export.png")),
        "F1 export remains pixel-identical after recipe reopen");
    std::cout << "optical_diffusion_changed_fraction=" << changed_pixel_fraction
              << " mean_abs_delta8=" << mean_absolute_delta
              << " max_channel_delta8=" << maximum_channel_delta << '\n'
              << "Offscreen Qt Quick F1 apply, real-photo render, recipe reopen and exact PNG export passed; photographic="
              << photographic << '\n';
    return 0;
  }
  const auto baseline=EditorPreviewControllerTestAccess::state(*controller).layers;
  const auto baseline_preview=controller->displayPreviewSource();
  settle();
  const auto closed_picker_frame = window.grabWindow();
  CHECK(click(find("photoAddEffectButton")),"Open Qt Quick effect picker");
  auto* filter_picker = find("photoEffectPicker");
  CHECK(filter_picker && !filter_picker->property("modal").toBool() && !filter_picker->property("dim").toBool(),
      "Filter picker leaves the image undimmed and interactive");
  const auto open_picker_frame = window.grabWindow();
  CHECK(!closed_picker_frame.isNull() && !open_picker_frame.isNull(), "Filter brightness proof has rendered frames");
  const auto scale = open_picker_frame.devicePixelRatio();
  const QRect image_region(qRound(360 * scale), qRound(40 * scale), qRound(570 * scale), qRound(540 * scale));
  CHECK(closed_picker_frame.copy(image_region) == open_picker_frame.copy(image_region),
      "Opening the grouped picker preserves the actual canvas pixels without dimming");
  if (!evidence.isEmpty()) {
    CHECK(closed_picker_frame.save(evidence+"/filter-picker-closed-native.png"), "Save unobstructed canvas baseline");
    CHECK(open_picker_frame.save(evidence+"/filter-picker-grouped-native.png"), "Save grouped undimmed picker");
  }
  auto* category_picker = find("photoEffectCategoryPicker");
  CHECK(category_picker && click(category_picker), "Open filter groups with pointer events");
  int blur_category_index = -1;
  for (int i = 0; i < category_picker->property("count").toInt(); ++i) {
    QString category_text;
    CHECK(QMetaObject::invokeMethod(category_picker, "textAt", Q_RETURN_ARG(QString, category_text), Q_ARG(int, i)),
        "Read the filter group label");
    if (category_text == QStringLiteral("Blur")) blur_category_index = i;
  }
  CHECK(blur_category_index > 0 && click(find("photoEffectCategoryPickerOption" + QString::number(blur_category_index))),
      "Choose the Blur group with pointer events");
  CHECK(filter_picker->property("visible").toBool() && find("photoEffectChoice_motion_blur") && !find("photoEffectGroup_Film"),
      "Choosing a group filters the list without closing the filter browser");
  auto* category_view = qobject_cast<QQuickItem*>(find("photoEffectPickerBodyScrollView"));
  auto* category_choice = qobject_cast<QQuickItem*>(find("photoEffectChoice_motion_blur"));
  CHECK(category_view && category_choice && QRectF(0, 0, category_view->width(), category_view->height()).contains(
      category_choice->mapToItem(category_view, QPointF(category_choice->width() / 2, category_choice->height() / 2))),
      "Category filtering lays out the first group on screen without manual scrolling");
  if (!evidence.isEmpty())
    CHECK(window.grabWindow().save(evidence+"/filter-picker-blur-native.png"), "Save category-filtered native picker");
  CHECK(click(find("photoEffectChoice_motion_blur")) && !filter_picker->property("browsing").toBool(),
      "A filter in the chosen group is visible and responds to pointer events");
  CHECK(click(find("photoEffectChangeFilterButton")), "Return to browsing the chosen group");
  CHECK(click(find("photoEffectCategoryReset")), "Restore all filter groups without reopening the group menu");
  CHECK(click(find("photoEffectChoice_motion_blur")),"Choose motion blur with pointer events");
  CHECK(!filter_picker->property("dim").toBool() && !filter_picker->property("browsing").toBool(),
      "Live filter preview leaves dimming off and presents parameters directly");
  CHECK(edit("photoEffectParameter_distance","37.25") && edit("photoEffectParameter_angle","-48.5"),"Qt Quick motion parameter input");
  auto motion=controller->photoLayerEffects(0).last().toMap();
  CHECK(motion.value("distance").toDouble()==37.25 && motion.value("angle").toDouble()==-48.5,"Fractional motion parameters reach controller");
  QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QCoreApplication::sendEvent(&window,&escape);settle();
  CHECK(EditorPreviewControllerTestAccess::state(*controller).layers==baseline,"Escape restores exact baseline");
  CHECK(click(find("photoAddEffectButton")) && click(find("photoEffectChoice_motion_blur")),"Reopen motion preview");
  CHECK(edit("photoEffectParameter_distance","24") && edit("photoEffectParameter_angle","25"),"Set accepted motion values");
  CHECK(click(find("photoEffectPreviewApply")),"Apply the motion preview");
  const auto motion_layers=EditorPreviewControllerTestAccess::state(*controller).layers;
  CHECK(motion_layers!=baseline,"Apply retains effect");
  controller->undo();CHECK(EditorPreviewControllerTestAccess::state(*controller).layers==baseline,"Apply and all parameters use one undo");
  controller->redo();CHECK(EditorPreviewControllerTestAccess::state(*controller).layers==motion_layers,"Redo preserves effect identity");
  CHECK(wait_until([&]{return controller->displayPreviewSource()!=baseline_preview && !controller->processing() && !controller->fullRenderRunning();}),"Accepted motion preview publishes");
  if(!evidence.isEmpty()) { settle();CHECK(window.grabWindow().save(evidence+"/motion-native.png"),"Motion native screenshot"); }
  CHECK(click(find("photoAddEffectButton")) && click(find("photoEffectChoice_unsharp_mask")),"Qt Quick unsharp preview");
  CHECK(edit("photoEffectParameter_sharpenRadius","2.75") && edit("photoEffectParameter_strength","185") &&
      edit("photoEffectParameter_threshold","2.5"),"Qt Quick sharpening controls");
  const auto sharp=controller->photoLayerEffects(0).last().toMap();
  CHECK(sharp.value("sharpenRadius").toDouble()==2.75 && sharp.value("strength").toDouble()==1.85 &&
      sharp.value("threshold").toDouble()==0.025,"Sharpen controls preserve fractional settings");
  if(!evidence.isEmpty()) { settle();CHECK(window.grabWindow().save(evidence+"/unsharp-picker-native.png"),"Native unsharp picker screenshot"); }
  CHECK(click(find("photoEffectPreviewApply")),"Apply unsharp");
  for (const auto& prior : controller->photoLayerEffects(0))
    controller->setPhotoLayerEffectEnabled(0, prior.toMap().value("id").toString(), false);
  CHECK(click(find("photoAddEffectButton")),"Open optical diffusion picker");
  auto* effect_search=find("photoEffectSearchField");
  CHECK(effect_search,"Filter search field exists");
  effect_search->setProperty("text",QStringLiteral("optical"));
  settle();
  const auto before_diffusion_preview=controller->displayPreviewSource();
  CHECK(click(find("photoEffectChoice_optical_diffusion")),"Qt Quick optical diffusion preview");
  const auto review_parameter=[](const char* name,double fallback) {
    bool ok=false;
    const auto value=qEnvironmentVariable(name).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
  };
  const auto review_radius=review_parameter("PHOTO_EFFECT_REVIEW_RADIUS",31.5);
  const auto review_strength=review_parameter("PHOTO_EFFECT_REVIEW_STRENGTH",140.0);
  const auto review_warmth=review_parameter("PHOTO_EFFECT_REVIEW_WARMTH",30.0);
  const auto review_protection=review_parameter("PHOTO_EFFECT_REVIEW_PROTECTION",65.0);
  CHECK(edit("photoEffectParameter_diffusionRadius",QString::number(review_radius,'g',16)) &&
      edit("photoEffectParameter_highlightThreshold","0.8") &&
      edit("photoEffectParameter_highlightKnee","0.25") &&
      edit("photoEffectParameter_diffusionStrength",QString::number(review_strength,'g',16)) &&
      edit("photoEffectParameter_warmth",QString::number(review_warmth,'g',16)) &&
      edit("photoEffectParameter_highlightProtection",QString::number(review_protection,'g',16)),"Qt Quick optical diffusion controls");
  const auto diffusion=controller->photoLayerEffects(0).last().toMap();
  CHECK(diffusion.value("type").toString()==QStringLiteral("optical_diffusion") &&
      std::abs(diffusion.value("diffusionRadius").toDouble()-review_radius)<1e-6 &&
      diffusion.value("highlightThreshold").toDouble()==0.8 &&
      diffusion.value("highlightKnee").toDouble()==0.25 &&
      diffusion.value("amount").toDouble()==1.0 &&
      std::abs(diffusion.value("diffusionStrength").toDouble()-review_strength/100.0)<1e-6 &&
      std::abs(diffusion.value("warmth").toDouble()-review_warmth/100.0)<1e-6 &&
      std::abs(diffusion.value("highlightProtection").toDouble()-review_protection/100.0)<1e-6,
      "Optical diffusion values reach the effect stack");
  CHECK(wait_until([&]{return controller->displayPreviewSource()!=before_diffusion_preview &&
      !controller->processing() && !controller->fullRenderRunning();}),"Optical diffusion preview settles before visual capture");
  if(!evidence.isEmpty()) { settle();CHECK(window.grabWindow().save(evidence+"/diffusion-qt-offscreen.png"),"Offscreen Qt optical diffusion screenshot"); }
  CHECK(click(find("photoEffectPreviewApply")),"Apply optical diffusion");
  const auto diffusion_id=diffusion.value("id").toString();
  const auto diffusion_state=EditorPreviewControllerTestAccess::state(*controller);
  auto diffusion_baseline_state=diffusion_state;
  diffusion_baseline_state.layers.clear();
  const auto diffusion_only=render_output_image_with_backend(
      source,diffusion_state,QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64,
      RenderBackendPreference::CpuOnly);
  const auto diffusion_baseline=render_output_image_with_backend(
      source,diffusion_baseline_state,QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64,
      RenderBackendPreference::CpuOnly);
  CHECK(!diffusion_only.isNull() && !diffusion_baseline.isNull() &&
      diffusion_only.size()==source.size() && diffusion_baseline.size()==source.size() &&
      diffusion_only!=diffusion_baseline,
      "Optical diffusion must visibly change the full-photo CPU output on the real photographic fixture");
  if(!evidence.isEmpty()) {
    CHECK(diffusion_only.save(evidence+"/diffusion-only-export.png"),"Save isolated optical diffusion output");
    CHECK(diffusion_baseline.save(evidence+"/diffusion-only-baseline.png"),"Save isolated optical diffusion baseline");
    QImage diffusion_difference(diffusion_only.size(),QImage::Format_RGBA8888);
    std::uint64_t changed_pixels=0;
    std::uint64_t channel_samples=0;
    std::uint64_t absolute_delta_sum=0;
    int maximum_channel_delta=0;
    for(int y=0;y<diffusion_only.height();++y) for(int x=0;x<diffusion_only.width();++x) {
      const auto before=diffusion_baseline.pixelColor(x,y);
      const auto after=diffusion_only.pixelColor(x,y);
      const auto red_delta=std::abs(before.red()-after.red());
      const auto green_delta=std::abs(before.green()-after.green());
      const auto blue_delta=std::abs(before.blue()-after.blue());
      const auto pixel_max=std::max({red_delta,green_delta,blue_delta});
      maximum_channel_delta=std::max(maximum_channel_delta,pixel_max);
      absolute_delta_sum+=static_cast<std::uint64_t>(red_delta+green_delta+blue_delta);
      channel_samples+=3;
      if(pixel_max>0) ++changed_pixels;
      diffusion_difference.setPixelColor(x,y,QColor(
          std::min(255,red_delta*8),std::min(255,green_delta*8),
          std::min(255,blue_delta*8),255));
    }
    const auto mean_absolute_delta=channel_samples == 0 ? 0.0 :
        static_cast<double>(absolute_delta_sum)/static_cast<double>(channel_samples);
    const auto changed_pixel_fraction=diffusion_only.isNull() ? 0.0 :
        static_cast<double>(changed_pixels)/static_cast<double>(diffusion_only.width()*diffusion_only.height());
    CHECK(diffusion_difference.save(evidence+"/diffusion-only-difference.png"),"Save isolated optical diffusion difference image");
    QFile metrics(evidence+"/diffusion-only-metrics.json");
    CHECK(metrics.open(QIODevice::WriteOnly),"Open isolated optical diffusion metrics");
    const auto metrics_json=QStringLiteral(
        "{\"meanAbsoluteChannelDelta8Bit\":%1,\"maximumChannelDelta8Bit\":%2,\"changedPixelFraction\":%3,\"changedPixels\":%4,\"width\":%5,\"height\":%6}\n")
        .arg(mean_absolute_delta,0,'g',12).arg(maximum_channel_delta)
        .arg(changed_pixel_fraction,0,'g',12).arg(changed_pixels)
        .arg(diffusion_only.width()).arg(diffusion_only.height());
    CHECK(metrics.write(metrics_json.toUtf8())==metrics_json.size(),"Write isolated optical diffusion metrics");
    std::cout << "optical_diffusion_changed_fraction=" << changed_pixel_fraction
              << " mean_abs_delta8=" << mean_absolute_delta
              << " max_channel_delta8=" << maximum_channel_delta << '\n';
  }
  CHECK(controller->resetPhotoLayerEffect(0,diffusion_id),"Reset optical diffusion parameters");
  const auto reset_diffusion=controller->photoLayerEffects(0).last().toMap();
  CHECK(reset_diffusion.value("diffusionRadius").toDouble()==24 &&
      reset_diffusion.value("highlightThreshold").toDouble()==0.8 &&
      reset_diffusion.value("highlightKnee").toDouble()==0.25 &&
      reset_diffusion.value("diffusionStrength").toDouble()==0.8 &&
      reset_diffusion.value("warmth").toDouble()==0.2 &&
      reset_diffusion.value("highlightProtection").toDouble()==0.0,"Reset restores optical diffusion defaults");
  controller->undo();
  CHECK(controller->photoLayerEffects(0).last().toMap()==diffusion,"Undo restores all optical diffusion values");
  CHECK(click(find("photoAddEffectButton")),"Open the imperfect print filter picker");
  effect_search=find("photoEffectSearchField");
  CHECK(effect_search,"Filter search remains available for print effects");
  effect_search->setProperty("text",QStringLiteral("imperfect print"));
  settle();
  CHECK(click(find("photoEffectChoice_imperfect_print")),"Choose imperfect print");
  CHECK(edit("photoEffectParameter_paperScale","18") &&
      edit("photoEffectParameter_paperTexture","42") &&
      edit("photoEffectParameter_inkSpread","30") &&
      edit("photoEffectParameter_inkDensity","120") &&
      edit("photoEffectParameter_redRegistrationX","-0.7") &&
      edit("photoEffectParameter_redRegistrationY","0.25") &&
      edit("photoEffectParameter_blueRegistrationX","0.45") &&
      edit("photoEffectParameter_blueRegistrationY","-0.15") &&
      edit("photoEffectParameter_seed","4294967295"),"Edit all imperfect print controls in the Qt workflow");
  const auto print=controller->photoLayerEffects(0).last().toMap();
  CHECK(print.value("type").toString()==QStringLiteral("imperfect_print") &&
      print.value("paperScale").toDouble()==18 && print.value("paperTexture").toDouble()==0.42 &&
      print.value("inkSpread").toDouble()==0.3 && print.value("inkDensity").toDouble()==1.2 &&
      print.value("redRegistrationX").toDouble()==-0.7 && print.value("redRegistrationY").toDouble()==0.25 &&
      print.value("blueRegistrationX").toDouble()==0.45 && print.value("blueRegistrationY").toDouble()==-0.15 &&
      print.value("amount").toDouble()==1.0 && print.value("enabled").toBool() &&
      print.value("seed").toULongLong()==4294967295ULL,
      "Imperfect print controls reach the user effect stack as typed values");
  if(!evidence.isEmpty()) { settle();CHECK(window.grabWindow().save(evidence+"/imperfect-print-qt-offscreen.png"),"Offscreen Qt imperfect print screenshot"); }
  CHECK(click(find("photoEffectPreviewApply")),"Apply imperfect print");
  const auto print_id=print.value("id").toString();
  CHECK(controller->resetPhotoLayerEffect(0,print_id),"Reset imperfect print settings");
  const auto reset_print=controller->photoLayerEffects(0).last().toMap();
  CHECK(reset_print.value("paperScale").toDouble()==32 && reset_print.value("paperTexture").toDouble()==0.2 &&
      reset_print.value("inkSpread").toDouble()==0 && reset_print.value("inkDensity").toDouble()==1 &&
      reset_print.value("seed").toUInt()==1U,"Reset restores the complete imperfect print defaults");
  controller->undo();
  CHECK(controller->photoLayerEffects(0).last().toMap()==print,"Undo restores every imperfect print parameter");
  CHECK(click(find("photoAddEffectButton")),"Open the creative effect picker for dragged shutter");
  effect_search=find("photoEffectSearchField");
  CHECK(effect_search,"Dragged shutter remains discoverable in the filter picker");
  effect_search->setProperty("text",QStringLiteral("dragged shutter"));
  settle();
  CHECK(click(find("photoEffectChoice_dragged_shutter")),"Choose dragged shutter");
  CHECK(edit("photoEffectParameter_distance","84") &&
      edit("photoEffectParameter_angle","-14") &&
      edit("photoEffectParameter_falloff","72") &&
      edit("photoEffectParameter_highlightWeighting","35"),
      "Edit trail length, direction, falloff, and highlight weighting in the Qt workflow");
  if(!review_without_mask)
    CHECK(scroll_into_view(find("photoEffectPreviewMaskFromSelection")) &&
        click(find("photoEffectPreviewMaskFromSelection")),
        "Attach the active subject selection to dragged shutter in the Qt workflow");
  if(!review_without_mask)
    CHECK(scroll_into_view(find("photoEffectProtectSubjectCheckBox")) &&
        click(find("photoEffectProtectSubjectCheckBox")),
        "Enable protected-subject behavior from the user checkbox");
  const auto shutter=controller->photoLayerEffects(0).last().toMap();
  CHECK(shutter.value("type").toString()==QStringLiteral("dragged_shutter") &&
      shutter.value("distance").toDouble()==84 && shutter.value("angle").toDouble()==-14 &&
      shutter.value("falloff").toDouble()==0.72 && shutter.value("highlightWeighting").toDouble()==0.35 &&
      shutter.value("protectSubject").toBool()==!review_without_mask &&
      (review_without_mask ? shutter.value("maskId").toString().isEmpty()
                           : !shutter.value("maskId").toString().isEmpty()),
      "Dragged shutter controls and protected subject mask reach the user effect stack as typed values");
  if(!evidence.isEmpty()) { settle();CHECK(window.grabWindow().save(evidence+"/dragged-shutter-qt-offscreen.png"),"Offscreen Qt dragged shutter screenshot"); }
  CHECK(click(find("photoEffectPreviewApply")),"Apply dragged shutter");
  const auto shutter_id=shutter.value("id").toString();
  CHECK(controller->resetPhotoLayerEffect(0,shutter_id),"Reset dragged shutter values");
  const auto reset_shutter=controller->photoLayerEffects(0).last().toMap();
  CHECK(reset_shutter.value("distance").toDouble()==32 && reset_shutter.value("angle").toDouble()==0 &&
      reset_shutter.value("falloff").toDouble()==0.65 && reset_shutter.value("highlightWeighting").toDouble()==0 &&
      !reset_shutter.value("protectSubject").toBool(),"Reset restores dragged shutter defaults");
  controller->undo();
  CHECK(controller->photoLayerEffects(0).last().toMap()==shutter,"Undo restores dragged shutter controls");
  const auto accepted=EditorPreviewControllerTestAccess::state(*controller).layers;
  const auto effect_id=sharp.value("id").toString();
  CHECK(controller->duplicatePhotoLayerEffect(0,effect_id),"Duplicate spatial effect");
  CHECK(controller->movePhotoLayerEffect(0,controller->photoLayerEffects(0).last().toMap().value("id").toString(),0),"Reorder spatial effect");
  controller->undo();controller->undo();CHECK(EditorPreviewControllerTestAccess::state(*controller).layers==accepted,"Undo reorder and duplicate");
  const auto state=EditorPreviewControllerTestAccess::state(*controller);
  QFile recipe(temp.filePath("recipe.json"));
  CHECK(recipe.open(QIODevice::WriteOnly) && recipe.write(serialize_edit_recipe_json(state).toUtf8())>0,"Persist recipe file");recipe.close();
  if(!evidence.isEmpty()) {
    QFile saved_recipe(evidence+"/saved-recipe.json");
    CHECK(saved_recipe.open(QIODevice::WriteOnly) &&
        saved_recipe.write(serialize_edit_recipe_json(state).toUtf8())>0,"Save creative effect recipe evidence");
  }
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
    QImage difference(exported.size(),QImage::Format_RGBA8888);
    for(int y=0;y<exported.height();++y) for(int x=0;x<exported.width();++x) {
      const auto before=plain.pixelColor(x,y), after=exported.pixelColor(x,y);
      difference.setPixelColor(x,y,QColor(
          std::min(255,std::abs(before.red()-after.red())*4),
          std::min(255,std::abs(before.green()-after.green())*4),
          std::min(255,std::abs(before.blue()-after.blue())*4),255));
    }
    CHECK(difference.save(evidence+"/effect-stack-difference.png"),"Effect difference evidence");
    settle();CHECK(window.grabWindow().save(evidence+"/stack-native.png"),"Offscreen stack screenshot");
  }
  std::cout << "Offscreen Qt Quick pointer/key preview, Cancel/Apply, fractional controls, undo/redo, duplicate/reorder, disk reopen and exact PNG export passed; photographic=" << photographic << '\n';
  return 0;
}
