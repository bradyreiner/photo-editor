#include "app_bootstrap.h"
#include "desktop_pipeline_state_compiler.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "photo_layer_compiler.h"
#include "photo_render_utils.h"
#include "photo_pixel_stroke.h"
#include "working_image.h"
#include "tablet_input_area.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QFile>
#include <iostream>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& c) {
    auto state=c.currentState();
    for(int i=0;i<state.layers.size();++i) state.layers[i]=photo_paint_target_identity(state.layers[i].toMap());
    return state;
  }
  static bool paintRasterIdle(const EditorPreviewController& c) {
    return !c.photo_paint_worker_running_ && (c.photo_paint_jobs_.empty() || c.photo_paint_jobs_.front()->pending_points.empty());
  }
  static RenderRequest request(const EditorPreviewController& c) { return c.currentRenderRequest(false); }
  static void apply(EditorPreviewController& c, const EditState& s) { c.applyState(s); }
};
}
using namespace photo_editor::desktop;
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

static bool wait_until(const std::function<bool()>& predicate) {
  QElapsedTimer timer; timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (predicate()) return true;
    QThread::msleep(5);
  } while (timer.elapsed() < 15000);
  return false;
}

static bool regional_matches_full(const RenderRequest& request, const QVariantList& base_layers) {
  auto region_request = request;
  region_request.use_in_memory_preview=true;
  auto base_request=region_request;
  base_request.state.layers=base_layers;
  base_request.recipe_cache_identity.clear();
  base_request.adjusted_recipe_cache_identity.clear();
  const auto base_render=render_preview_request_for_test(base_request);
  region_request.paint_preview_base=base_render.preview_image;
  if (region_request.paint_preview_base.isNull()) return false;
  const auto regional=render_preview_request_for_test(region_request);
  if (!regional.success || regional.backend!="paint-region") return false;
  region_request.paint_preview_base={};
  region_request.recipe_cache_identity.clear();
  region_request.adjusted_recipe_cache_identity.clear();
  const auto full_draft=render_preview_request_for_test(region_request);
  if (!full_draft.success || full_draft.preview_image.size()!=regional.preview_image.size()) return false;
  int worst_channel_error=0, outside_error=0;
  for(int y=0;y<regional.preview_image.height();++y) for(int x=0;x<regional.preview_image.width();++x) {
    const auto a=regional.preview_image.pixelColor(x,y),b=full_draft.preview_image.pixelColor(x,y);
    const int delta=std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
    if (delta>worst_channel_error) worst_channel_error=delta;
    if (!regional.texture_upload_rect.contains(x,y)) outside_error=std::max(outside_error,delta);
  }
  std::cout << "regional_max_channel_error=" << worst_channel_error
            << " outside_error=" << outside_error << '\n';
  return worst_channel_error <= 1;
}

int main(int argc, char** argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  QTemporaryDir temp;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temp.path().toUtf8());
  CHECK(AppBootstrap::ensureRuntimeDirectories(), "Runtime directories");
  const QVariantMap empty{{"pixelWidth",640},{"pixelHeight",400},
      {"paintTiles",empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
  const auto raster = [](const PhotoPixelStroke& value) { return value.surface.raster({},nullptr,value.draft_tiles()); };
  PhotoPixelStroke stroke;
  stroke.color = QColor("#ff4000"); stroke.diameter = 40; stroke.opacity = 0.5;
  stroke.pressure_size = false;
  CHECK(stroke.begin(empty), "Stroke initialization");
  stroke.point({80,100},1); stroke.point({400,100},1); stroke.point({80,100},1); stroke.finish();
  const auto stroke_image = raster(stroke);
  CHECK(qAlpha(stroke_image.pixel(200,100)) == 128, "Overlapping dabs must respect stroke opacity");
  CHECK(qAlpha(stroke_image.pixel(200,121)) == 0, "Circular brush must remain local on non-square images");
  PhotoPixelStroke segmented;
  segmented.color=stroke.color; segmented.diameter=40; segmented.opacity=0.5; segmented.pressure_size=false;
  CHECK(segmented.begin(empty), "Segmented initialization"); segmented.point({80,100},1);
  for(int x=81;x<=400;++x) segmented.point({double(x),100},1);
  for(int x=399;x>=80;--x) segmented.point({double(x),100},1);
  segmented.finish();
  CHECK(raster(segmented)==stroke_image, "Pointer event density must not change the stroke");
  auto colored=empty; colored.insert("paintTiles",stroke.commit());
  PhotoPixelStroke eraser;
  eraser.erase=true; eraser.opacity=0.5; eraser.diameter=40; eraser.pressure_size=false;
  CHECK(eraser.begin(colored), "Eraser initialization"); eraser.point({200,100},1);
  const auto erased_image=raster(eraser);
  CHECK(qAlpha(erased_image.pixel(200,100))==64 && erased_image.pixel(90,100)==stroke_image.pixel(90,100),
      "Eraser must preserve pixels beyond its footprint");
  const auto earlier_tiles=colored.value("paintTiles").toMap().value("tiles").toMap();
  const auto later_tiles=eraser.commit().value("tiles").toMap();
  const auto untouched=PhotoPaintSurface::key(0,0);
  CHECK(earlier_tiles.value(untouched).toString().constData()==later_tiles.value(untouched).toString().constData(),
      "Undo states must share unchanged compressed tile storage");
  PhotoPixelStroke clipped; clipped.diameter=80; clipped.pressure_size=false;
  clipped.selection=QImage(640,400,QImage::Format_Grayscale8); clipped.selection.fill(0);
  for(int y=0;y<400;++y) for(int x=0;x<320;++x) clipped.selection.scanLine(y)[x]=128;
  CHECK(clipped.begin(empty), "Clipped initialization"); clipped.point({320,200},1);
  const auto clipped_image=raster(clipped);
  CHECK(qAlpha(clipped_image.pixel(310,200))==128 && qAlpha(clipped_image.pixel(321,200))==0,
      "Soft selections must clip paint without crossing boundaries");
  PhotoPixelStroke pressure; pressure.diameter=40;
  CHECK(pressure.begin(empty), "Pressure initialization"); pressure.point({50,50},0.25); pressure.point({150,50},1);
  const auto pressure_image=raster(pressure);
  CHECK(qAlpha(pressure_image.pixel(50,58))==0 && qAlpha(pressure_image.pixel(140,58))>0, "Pressure radius");
  QVariantMap transform{{"pixelWidth",640},{"pixelHeight",400},{"x",20},{"y",30},{"scale",0.5},{"rotation",90}};
  PhotoPixelStroke transformed; transformed.diameter=10; transformed.pressure_size=false;
  transformed.layer_to_document=photo_pixel_layer_transform(transform);
  CHECK(transformed.begin(empty), "Transform initialization");
  transformed.point(transformed.layer_to_document.map(QPointF(100,80)),1);
  CHECK(qAlpha(raster(transformed).pixel(100,80))>0, "Inverse layer transform");
  auto wide=empty;
  wide.insert("paintTiles",empty_photo_paint_tiles(working_color_space("Linear Display P3")));
  PhotoPaintSurface high_precision; CHECK(high_precision.load(wide), "Wide-gamut surface");
  auto hdr_tile=high_precision.tile(0,0);
  auto* hdr=reinterpret_cast<QRgbaFloat32*>(hdr_tile.bits());
  const QRgbaFloat32 hdr_value{1.678901F,-0.012345F,0.1234567F,0.876543F};
  for(int i=0;i<hdr_tile.width()*hdr_tile.height();++i) hdr[i]=hdr_value;
  high_precision.store(0,0,hdr_tile); wide.insert("paintTiles",high_precision.snapshot());
  const auto hdr_reopened=rasterize_photo_paint_layer(wide);
  CHECK(*reinterpret_cast<const QRgbaFloat32*>(hdr_reopened.constBits())==hdr_value,
      "Float tiles must preserve negative, HDR and sub-16-bit values exactly");
  PhotoPixelStroke hdr_eraser; hdr_eraser.erase=true; hdr_eraser.diameter=20; hdr_eraser.opacity=0.123456;
  hdr_eraser.pressure_size=false; CHECK(hdr_eraser.begin(wide), "HDR erase"); hdr_eraser.point({50,50},1);
  const auto hdr_result=raster(hdr_eraser);
  const auto result_pixel=reinterpret_cast<const QRgbaFloat32*>(hdr_result.constScanLine(50))[50];
  CHECK(std::abs(result_pixel.red()-hdr_value.red()*0.876544F)<1e-6F && result_pixel.green()<0,
      "Erasing must preserve floating precision and extended gamut");
  const auto wide_durable=hdr_eraser.commit();
  wide.insert("paintTiles",wide_durable);
  EditRecipeState hdr_state; hdr_state.layers={wide};
  const auto hdr_json=serialize_edit_recipe_json(hdr_state);
  CHECK(validate_edit_recipe_json(hdr_json).valid, "Version 16 paint recipe validates");
  CHECK(!migrate_edit_recipe_json(hdr_json,15).errors.isEmpty(), "Older recipe versions must not silently discard paint");
  const auto hdr_parsed=parse_edit_recipe_json(hdr_json);
  CHECK(rasterize_photo_paint_layer(hdr_parsed.layers[0].toMap())==hdr_result, "Float tile recipe round-trip exactness");
  auto corrupt=wide; auto payload=wide.value("paintTiles").toMap(); auto bad_tiles=payload.value("tiles").toMap();
  bad_tiles.insert("0,0","invalid"); payload.insert("tiles",bad_tiles); corrupt.insert("paintTiles",payload);
  QString corruption_error;
  CHECK(rasterize_photo_paint_layer(corrupt,{},&corruption_error).isNull() && !corruption_error.isEmpty(), "Corrupt tiles must fail explicitly");
  auto large=empty; large.insert("pixelWidth",16000); large.insert("pixelHeight",12000);
  PhotoPixelStroke sparse; CHECK(sparse.begin(large), "192 MP sparse canvas initialization"); sparse.diameter=32; sparse.point({1000,1000},1);
  CHECK(sparse.changed_tile_count()<=4 && sparse.commit().value("tiles").toMap().size()<=4,
      "A small stroke on a 192 MP canvas must allocate only touched tiles");

  QImage source(640,400,QImage::Format_ARGB32); source.fill(QColor("#304052"));
  const auto photo_path = qEnvironmentVariable("PHOTO_PAINT_TEST_SOURCE");
  if (!photo_path.isEmpty()) {
    QImage photo(photo_path);
    CHECK(!photo.isNull(), "Photographic proof fixture loads");
    source = photo.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path = temp.filePath("source.png"); CHECK(source.save(source_path), "Source PNG");
  QQmlEngine engine;
  auto* provider = new EditorPreviewImageProvider;
  engine.addImageProvider("editor-preview", provider);
  EditorPreviewController controller;
  controller.setImageProvider(provider);
  controller.loadAsset("paint-proof", source_path, {}, {}, {}, {}, {});
  CHECK(wait_until([&] { return !controller.displayPreviewSource().isEmpty(); }), "Source preview");
  CHECK(controller.addPaintLayer(), "Transparent layer creation");
  using Access = EditorPreviewControllerTestAccess;
  const auto blank = Access::state(controller);
  const QVariantMap settings{{"color","#ff7040"},{"size",40},{"opacity",0.65},{"pressureSize",false}};
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Controller begins stroke");
  controller.addPhotoPaintPoint(100,100); controller.addPhotoPaintPoint(400,100);
  CHECK(Access::state(controller).layers == blank.layers, "Draft must not mutate persisted state");
  CHECK(wait_until([&] { return Access::request(controller).state.layers[0].toMap().contains("paintDraftTiles"); }), "Worker publishes draft tiles");
  CHECK(wait_until([&] { return Access::paintRasterIdle(controller); }), "All pending stroke samples reach the draft");
  const auto draft_request = Access::request(controller);
  const auto draft_layers = compile_pipeline_photo_layers(draft_request.state.layers, {},
      LocalMaskRasterPolicy::UseStoredRaster, QSize(640,400), {}, {}, DesktopRenderPurpose::Preview);
  CHECK(draft_layers.size() == 1 && draft_layers[0].pixels[100 * 640 + 200].alpha > 0,
      "Draft image must reach actual layer compilation before pointer release");
  CHECK(!draft_request.state.layers[0].toMap().value("paintDraftTiles").toMap().isEmpty(), "Draft renderer must see live pixels");
  CHECK(regional_matches_full(draft_request, blank.layers),
        "Regional paint must agree with the complete color-managed preview");
  CHECK(controller.finishPhotoPaintStroke(), "Stroke commit");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(), "Async paint commit completes");
  const auto painted = Access::state(controller);
  const auto painted_image = rasterize_photo_paint_layer(painted.layers[0].toMap());
  CHECK(qAlpha(painted_image.pixel(200,100)) > 0, "Committed color pixels");
  controller.undo(); CHECK(Access::state(controller).layers == blank.layers, "One undo restores entire stroke");
  controller.redo(); CHECK(Access::state(controller).layers == painted.layers, "Redo restores pixels");
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Cancel begins"); controller.addPhotoPaintPoint(100,200);
  controller.finishPhotoPaintStroke(false);
  CHECK(Access::state(controller).layers == painted.layers, "Cancel preserves durable image");
  auto clipped_settings = settings; clipped_settings.insert("clipSelection", true);
  CHECK(!controller.beginPhotoPaintStroke(0,clipped_settings), "Missing selection must never paint unclipped");
  controller.addMask("rectangle");
  controller.setActiveMaskFeather(0);
  controller.setActiveMaskCenterX(0.25); controller.setActiveMaskCenterY(0.5);
  controller.setActiveMaskRadiusX(0.25); controller.setActiveMaskRadiusY(0.5);
  int paint_index = -1;
  const auto selection_state = Access::state(controller);
  for (int i = 0; i < selection_state.layers.size(); ++i)
    if (selection_state.layers[i].toMap().value("id") == painted.layers[0].toMap().value("id")) paint_index = i;
  CHECK(controller.beginPhotoPaintStroke(paint_index,clipped_settings),
      "Actual canonical selection clipping: " << controller.status().toStdString());
  controller.addPhotoPaintPoint(100,200); controller.addPhotoPaintPoint(500,200);
  CHECK(controller.finishPhotoPaintStroke(), "Selection-clipped stroke commit");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(), "Async paint commit completes");
  const auto selection_pixels = rasterize_photo_paint_layer(Access::state(controller).layers[paint_index].toMap());
  CHECK(qAlpha(selection_pixels.pixel(150,200)) > 0 && qAlpha(selection_pixels.pixel(400,200)) == 0,
      "Controller stroke must honor the real active selection boundary");
  Access::apply(controller, painted);
  auto locked = painted; auto layer = locked.layers[0].toMap(); layer.insert("locked",true); locked.layers[0] = layer;
  Access::apply(controller, locked);
  CHECK(!controller.beginPhotoPaintStroke(0,settings), "Locked pixels must reject strokes");
  Access::apply(controller, painted);
  auto erased_settings = settings; erased_settings.insert("erase",true); erased_settings.insert("opacity",0.5);
  const auto before_erase = Access::state(controller);
  CHECK(controller.beginPhotoPaintStroke(0,erased_settings), "Controller erase"); controller.addPhotoPaintPoint(200,100);
  CHECK(wait_until([&] { return Access::paintRasterIdle(controller) &&
        Access::request(controller).state.layers[0].toMap().contains("paintDraftTiles"); }), "Eraser draft settles");
  CHECK(regional_matches_full(Access::request(controller), before_erase.layers),
        "Regional erasing must agree with the complete color-managed preview");
  CHECK(controller.finishPhotoPaintStroke(), "Erase commit");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(), "Async paint commit completes");
  const auto erased = Access::state(controller);
  CHECK(qAlpha(rasterize_photo_paint_layer(erased.layers[0].toMap()).pixel(200,100)) <
        qAlpha(painted_image.pixel(200,100)), "Controller erases actual pixels");
  const auto serialized = serialize_edit_recipe_json(erased);
  const auto reopened = parse_edit_recipe_json(serialized);
  CHECK(reopened.layers[0].toMap().value("paintTiles") == erased.layers[0].toMap().value("paintTiles"), "Reopen preserves immutable tile bytes");
  QFile saved_recipe(temp.filePath("paint-recipe.json"));
  CHECK(saved_recipe.open(QIODevice::WriteOnly) && saved_recipe.write(serialized.toUtf8()) > 0, "Write recipe to disk");
  saved_recipe.close();
  CHECK(saved_recipe.open(QIODevice::ReadOnly), "Read saved recipe");
  auto reopened_controller = std::make_unique<EditorPreviewController>();
  reopened_controller->loadAsset("paint-reopened", source_path, {}, {}, {}, {}, QString::fromUtf8(saved_recipe.readAll()));
  CHECK(Access::state(*reopened_controller).layers[0].toMap().value("paintTiles") == erased.layers[0].toMap().value("paintTiles"),
      "A new controller must restore painted pixels from the saved document");
  reopened_controller.reset();
  const auto output = render_output_image_with_backend(source, erased, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  const auto restored = render_output_image_with_backend(source, reopened, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  CHECK(!output.isNull() && output == restored, "Output must exactly survive save and reopen");
  const auto compiled = compile_pipeline_photo_layers(erased.layers, {}, LocalMaskRasterPolicy::UseStoredRaster,
      QSize(320,200), {}, {}, DesktopRenderPurpose::Preview);
  CHECK(compiled.size() == 1 && compiled[0].pixel_width == 320 && compiled[0].pixel_height == 200,
      "Pixel layers must scale with preview resolution");
  CHECK(compiled[0].pixels[50 * 320 + 150].alpha > 0, "Half-size preview stroke must stay aligned");
  const auto export_path = temp.filePath("paint-export.png");
  CHECK(controller.exportFlattenedPhoto(export_path), "Start actual PNG export");
  CHECK(wait_until([&] { return !controller.photoExportBusy(); }) && controller.photoExportError().isEmpty(), "PNG export completes");
  CHECK(!QImage(export_path).isNull(), "Exported file is readable");
  const auto layer_export_path = temp.filePath("paint-layer-export.png");
  CHECK(controller.startPhotoLayerExport(0,layer_export_path), "Start asynchronous float paint layer export");
  CHECK(wait_until([&] { return !controller.photoExportBusy(); }) && controller.photoExportError().isEmpty(),
      "Paint layer export completes");
  const QImage layer_export(layer_export_path);
  CHECK(layer_export.depth()==64 && layer_export.colorSpace().isValid(),
      "Float paint PNG export must retain 16-bit channels and its color profile");
  CHECK(std::abs(layer_export.pixelColor(300,100).alphaF()-
      rasterize_photo_paint_layer(erased.layers[0].toMap()).pixelColor(300,100).alphaF())<0.0001,
      "PNG export must retain paint alpha beyond 8-bit precision");

  const auto before_queue=Access::state(controller);
  CHECK(controller.beginPhotoPaintStroke(0,settings), "First queued stroke"); controller.addPhotoPaintPoint(100,250); controller.finishPhotoPaintStroke();
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Second queued stroke"); controller.addPhotoPaintPoint(500,250); controller.finishPhotoPaintStroke();
  CHECK(!controller.exportFlattenedPhoto(temp.filePath("pending.png")), "Export must not omit pending strokes");
  QString close_error; CHECK(!controller.prepareRecipeClose(&close_error), "Close must wait for pending paint");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(), "Queued strokes finish: " << controller.photoPaintError().toStdString());
  auto queued_image=rasterize_photo_paint_layer(Access::state(controller).layers[0].toMap());
  CHECK(qAlpha(queued_image.pixel(100,250))>0 && qAlpha(queued_image.pixel(500,250))>0, "Queued strokes must rebase without losing either result");
  controller.undo(); queued_image=rasterize_photo_paint_layer(Access::state(controller).layers[0].toMap());
  CHECK(qAlpha(queued_image.pixel(100,250))>0 && qAlpha(queued_image.pixel(500,250))==0, "Undo removes only the last queued stroke");
  controller.undo(); CHECK(Access::state(controller).layers==before_queue.layers, "Second undo restores prior tile map");
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Pending cancel"); controller.addPhotoPaintPoint(300,350); controller.finishPhotoPaintStroke(); controller.undo();
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && Access::state(controller).layers==before_queue.layers, "Undo can cancel an uncommitted worker stroke");

  qmlRegisterType<TabletInputArea>("PhotoEditor",1,0,"TabletInputArea");
  engine.rootContext()->setContextProperty("paintController", &controller);
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components"
Item {
    width: 800; height: 640
    Rectangle { anchors.fill: parent; color: "#181c21" }
    Image { x: 40; y: 30; width: 720; height: 450; source: paintController.displayPreviewSource; cache: false }
    PhotoPaintOverlay {
        objectName: "paintOverlay"; anchors.fill: parent
        active: true; controller: paintController; documentId: "paint-proof"
        layerIndex: 0; layerData: ({kind: "pixel", name: "Paint Layer"})
        imageX: 40; imageY: 30; imageWidth: 720; imageHeight: 450
        canvasWidth: 640; canvasHeight: 400
        ink: "#60d8bd"; brushSize: 32; pressureSize: false
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_PAINT_TEST_ROOT "/tests/paint-harness.qml")));
  std::unique_ptr<QObject> root(component.create());
  CHECK(root, component.errorString().toStdString());
  QQuickWindow window; window.resize(800,640);
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  window.show(); window.requestActivate();
  CHECK(wait_until([&] { return window.isExposed(); }), "Native paint window");
  auto* paintOverlay = root->findChild<QObject*>("paintOverlay");
  auto* paintAdvancedButton = root->findChild<QObject*>("photoPaintAdvancedButton");
  auto* paintAdvancedControls = root->findChild<QObject*>("photoPaintAdvancedControls");
  CHECK(paintOverlay && paintAdvancedButton && paintAdvancedControls, "Compact brush HUD controls instantiate");
  CHECK(!paintAdvancedControls->property("visible").toBool(), "Brush details start collapsed");
  CHECK(QMetaObject::invokeMethod(paintAdvancedButton, "clicked"), "Expand brush details");
  QCoreApplication::processEvents();
  CHECK(paintAdvancedControls->property("visible").toBool(), "Brush details expand on demand");
  CHECK(QMetaObject::invokeMethod(paintAdvancedButton, "clicked"), "Collapse brush details");
  QCoreApplication::processEvents();
  CHECK(!paintAdvancedControls->property("visible").toBool(), "Brush details collapse back to the compact HUD");
  auto mouse = [&](QEvent::Type type, QPointF p, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type,p,window.mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);
    QCoreApplication::sendEvent(&window,&event); QCoreApplication::processEvents();
  };
  CHECK(wait_until([&] { return !controller.processing() && !controller.fullRenderRunning(); }), "Settle before native input");
  const auto preview_before_drag = controller.displayPreviewSource();
  mouse(QEvent::MouseButtonPress,{180,270},Qt::LeftButton,Qt::LeftButton);
  mouse(QEvent::MouseMove,{550,310},Qt::NoButton,Qt::LeftButton);
  CHECK(root->findChild<QObject*>("paintOverlay")->property("drawing").toBool(), "Native canvas must own stroke");
  CHECK(wait_until([&] { return Access::request(controller).state.layers[0].toMap().contains("paintDraftTiles"); }), "Native drag reaches worker and renderer");
  CHECK(wait_until([&] { return controller.displayPreviewSource() != preview_before_drag && !controller.processing(); }),
      "Live pointer movement must publish a visible preview before release");
  mouse(QEvent::MouseButtonRelease,{550,310},Qt::LeftButton,Qt::NoButton);
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && Access::state(controller).layers != erased.layers, "Native pointer release commits pixels");
  const auto after_native = Access::state(controller);
  mouse(QEvent::MouseButtonPress,{200,350},Qt::LeftButton,Qt::LeftButton);
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window,&escape);
  mouse(QEvent::MouseButtonRelease,{200,350},Qt::LeftButton,Qt::NoButton);
  CHECK(Access::state(controller).layers == after_native.layers, "Escape discards live native stroke");
  const auto evidence = qEnvironmentVariable("PHOTO_PAINT_EVIDENCE_DIR");
  if (!evidence.isEmpty()) {
    QDir{}.mkpath(evidence);
    wait_until([&] { return !controller.processing() && !controller.fullRenderRunning(); });
    QElapsedTimer settle; settle.start();
    while (settle.elapsed() < 300) { QCoreApplication::processEvents(); QThread::msleep(5); }
    CHECK(window.grabWindow().save(evidence + "/native-paint-controls.png"), "Native screenshot");
    CHECK(output.save(evidence + "/paint-erase-output.png"), "Output evidence");
  }
  const auto before_navigation=Access::state(controller);
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Navigation stroke begins");
  controller.addPhotoPaintPoint(580,370); controller.finishPhotoPaintStroke();
  controller.loadAsset("next-photo",source_path,{},{},{},{},{});
  CHECK(wait_until([&]{return controller.assetId()=="next-photo" && !controller.photoPaintBusy();}),
      "Navigation waits for a released stroke");
  CHECK(Access::state(controller).layers.isEmpty(), "A pending stroke cannot land on the next photo");
  controller.loadAsset("paint-proof",source_path,{},{},{},{},{});
  CHECK(Access::state(controller).layers!=before_navigation.layers &&
      qAlpha(rasterize_photo_paint_layer(Access::state(controller).layers[0].toMap()).pixel(580,370))>0,
      "Returning to the original photo must retain the stroke released before navigation");
  const auto before_cancel=Access::state(controller);
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Cancel pending work begins");
  controller.addPhotoPaintPoint(580,20); controller.finishPhotoPaintStroke(); controller.cancelPhotoPaint();
  CHECK(!controller.photoPaintBusy() && Access::state(controller).layers==before_cancel.layers,
      "Explicit cancellation must discard queued work without altering saved pixels");
  std::cout << "Paint/erase, selection alpha, pressure, transforms, undo, recipe reopen, export and native pointer checks passed\n";
  return 0;
}
