#include "app_bootstrap.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "editor_preview_texture_item.h"
#include "editor_preview_texture.h"
#include "observability.h"
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QThread>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QBuffer>
#include <iostream>
#include <sys/resource.h>
using namespace photo_editor::desktop;
namespace photo_editor::desktop {
#if defined(PHOTO_PAINT_HAS_NATIVE_TEXTURE)
std::unique_ptr<EditorPreviewTexture> create_metal_editor_preview_texture();
#endif
struct EditorPreviewControllerTestAccess {
  static bool enableNativeTexture(EditorPreviewController& c) {
#if defined(PHOTO_PAINT_HAS_NATIVE_TEXTURE)
    c.display_preview_texture_ = create_metal_editor_preview_texture();
    return c.display_preview_texture_ && c.display_preview_texture_->available();
#else
    (void)c;
    return false;
#endif
  }
  static EditState state(const EditorPreviewController& c) { return c.currentState(); }
  static bool settled(const EditorPreviewController& c) {
    return !c.photo_paint_worker_running_ && !c.photo_paint_preview_pending_ &&
        !c.processing() && !c.fullRenderRunning();
  }
  static QJsonObject timings(const EditorPreviewController& c) {
    const auto r=c.interactive_render_watcher_.result();
    return {{"backend",r.backend},{"uploaded_pixels",r.texture_upload_pixels},{"render_ms",r.total_elapsed_ms},{"decode_ms",r.decode_elapsed_ms},
      {"edit_ms",r.edit_elapsed_ms},{"encode_ms",r.encode_elapsed_ms},
      {"pipeline_ms",r.adjustment_pipeline_elapsed_ms},{"queue_ms",r.queue_wait_ms},
      {"output_ms",r.output_stage_elapsed_ms},{"max_edge",r.max_edge},
      {"output_width",r.output_width},{"output_height",r.output_height}};
  }
};
}
static bool spin(const std::function<bool()>& done, int timeout = 45000) {
  QElapsedTimer t; t.start();
  do { QCoreApplication::processEvents(QEventLoop::AllEvents,5); if(done()) return true; QThread::msleep(1); } while(t.elapsed()<timeout);
  return false;
}
int main(int argc,char** argv) {
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE","1");
  QGuiApplication app(argc,argv);
  QTemporaryDir dir; qputenv("PHOTO_EDITOR_APP_DATA_DIR",dir.path().toUtf8());
  AppBootstrap::ensureRuntimeDirectories();
  observability::initialize();
  const int width = qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_WIDTH") > 0 ? qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_WIDTH") : 6000;
  QImage source(width,width*2/3,QImage::Format_RGB32); source.fill(QColor("#304050"));
  const auto photo_path=qEnvironmentVariable("PHOTO_PAINT_BENCH_SOURCE");
  if(!photo_path.isEmpty()) {
    QImage photo(photo_path); if(photo.isNull()) return 14;
    photo=photo.scaled(source.size(),Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
    source=photo.copy((photo.width()-width)/2,(photo.height()-width*2/3)/2,width,width*2/3);
  }
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto path=dir.filePath("large.png"); if(!source.save(path)) return 1;
  source = {};
  QQmlEngine engine;
  EditorPreviewController c; c.setPreviewTargetEdge(1024);
  const bool native_texture = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_NATIVE_TEXTURE");
  if (native_texture && !EditorPreviewControllerTestAccess::enableNativeTexture(c)) return 17;
  const bool memory_preview=!qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_FILE_PREVIEW");
  if(memory_preview) {
    auto* provider=new EditorPreviewImageProvider;
    engine.addImageProvider("editor-preview",provider); c.setImageProvider(provider);
  }
  c.loadAsset("paint-benchmark",path,{},{},{},{},{});
  if(!spin([&]{return !c.displayPreviewSource().isEmpty() && !c.processing() && !c.fullRenderRunning();})) return 2;
  engine.rootContext()->setContextProperty("paintController",&c);
  QQmlComponent component(&engine);
  component.setData("import QtQuick\nItem { width: 900; height: 600; Image { objectName: \"image\"; anchors.fill: parent; source: paintController.displayPreviewSource; cache: false } }",QUrl());
  std::unique_ptr<QObject> root(component.create()); if(!root) return 3;
  QQuickWindow window; window.resize(900,600); qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  if (native_texture) {
    if (!c.displayPreviewNativeAvailable()) return 17;
    root->findChild<QObject*>("image")->setProperty("visible", false);
    auto* item = new EditorPreviewTextureItem(window.contentItem());
    item->setSize(QSizeF(900,600));
    item->setController(&c);
  }
  window.show(); window.requestActivate();
  if(!spin([&]{return window.isExposed();})) return 4;
  QElapsedTimer timer; timer.start(); if(!c.addPaintLayer()) return 5;
  const auto create_ms=timer.nsecsElapsed()/1e6;
  if(!spin([&]{return !c.processing() && !c.fullRenderRunning();})) return 6;
  QJsonArray rows;
  int frames=0; QObject::connect(&window,&QQuickWindow::frameSwapped,&app,[&]{++frames;});
  for(int trial=0;trial<3;++trial) {
    const auto before=c.displayPreviewSource();
    const auto before_frame=frames;
    const auto before_native_revision=c.displayPreviewNativeRevision();
    timer.restart();
    if(!c.beginPhotoPaintStroke(0,{{"color",trial%2?"#20e0a0":"#ff5020"},{"size",96},{"pressureSize",false}})) return 7;
    const auto begin_ms=timer.nsecsElapsed()/1e6;
    QElapsedTimer input; input.start();
    c.addPhotoPaintPoint(width*0.25,width*0.2+trial*150);
    c.addPhotoPaintPoint(width*0.65,width*0.2+trial*150);
    const auto input_ms=input.nsecsElapsed()/1e6;
    if(!spin([&]{return (native_texture ? c.displayPreviewNativeRevision()!=before_native_revision :
        c.displayPreviewSource()!=before && root->findChild<QObject*>("image")->property("status").toInt()==1) && frames>before_frame;})) return 8;
    window.requestUpdate(); const auto published_frame=frames;
    if(!spin([&]{return frames>published_frame;})) return 9;
    const auto presented_ms=timer.nsecsElapsed()/1e6;
    const auto stages=EditorPreviewControllerTestAccess::timings(c);
    if(!spin([&]{return EditorPreviewControllerTestAccess::settled(c);})) return 15;
    window.requestUpdate(); const auto complete_frame=frames;
    if(!spin([&]{return frames>complete_frame;})) return 16;
    const auto complete_presented_ms=timer.nsecsElapsed()/1e6;
    input.restart(); if(!c.finishPhotoPaintStroke()) return 10;
    const auto release_ms=input.nsecsElapsed()/1e6;
    if(!spin([&]{return !c.property("photoPaintBusy").toBool();})) return 11;
    const auto commit_ms=input.nsecsElapsed()/1e6;
    const auto recipe=serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(c));
    rows.append(QJsonObject{{"stroke_complete_presented_ms",complete_presented_ms},{"stages",stages},{"begin_ms",begin_ms},{"input_batch_ms",input_ms},{"first_presented_ms",presented_ms},{"release_ms",release_ms},{"commit_complete_ms",commit_ms},{"recipe_bytes",recipe.toUtf8().size()}});
    if(!spin([&]{return !c.processing() && !c.fullRenderRunning();})) return 12;
  }
  struct rusage usage{}; getrusage(RUSAGE_SELF,&usage);
  QJsonObject result{{"native_texture_item",native_texture},{"in_memory_preview",memory_preview},{"photographic_fixture",!photo_path.isEmpty()},{"document_width",width},{"document_height",width*2/3},{"create_layer_ms",create_ms},{"peak_rss_bytes",double(usage.ru_maxrss)},{"trials",rows}};
  std::cout<<QJsonDocument(result).toJson().toStdString();
  const auto evidence=qEnvironmentVariable("PHOTO_PAINT_PERF_OUTPUT");
  if(!evidence.isEmpty()) { QFile file(evidence); if(!file.open(QIODevice::WriteOnly)) return 13; file.write(QJsonDocument(result).toJson()); window.grabWindow().save(evidence+".png"); }
  return 0;
}
