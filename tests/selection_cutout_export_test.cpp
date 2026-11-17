#include "editor_preview_controller.h"
#include "canonical_composite_mask_alpha.h"
#include "edit_adjustment_patch.h"
#include "mask_range_preview_math.h"
#include "photo_paint_surface.h"
#include "photo_render_utils.h"
#include <algorithm>
#include <cmath>
#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace photo_editor::desktop;
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void wait(EditorPreviewController& editor) {
  QElapsedTimer timer; timer.start();
  do { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
  while ((editor.processing() || editor.photoPaintBusy() || editor.photoExportBusy() || editor.selectionWorkBusy()) && timer.elapsed() < 60000);
  require(!editor.processing() && !editor.photoPaintBusy() && !editor.photoExportBusy() && !editor.selectionWorkBusy(), "Work must settle");
  require(editor.photoExportError().isEmpty() && editor.photoPaintError().isEmpty(), "Export and materialization must succeed");
}
QImage reference_cutout(const QString& path, const EditState& state, const QVariantList& masks) {
  auto base = render_editable_base_pixels(path, state, nullptr, true);
  const auto size = source_image_size(path);
  if (base.size() != size) base = base.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  QImage source;
  require(decode_source_image(path, &source, 0, state), "Reference source must decode");
  source = apply_crop(source, state);
  const auto alpha = evaluate_canonical_composite_mask_alpha(source, masks,
      std::pow(2.0, state.exposure), preview_white_balance_multipliers(state.temperature, state.tint));
  require(alpha.valid, "Canonical reference alpha must evaluate");
  auto coverage = QImage(reinterpret_cast<const uchar*>(alpha.alpha_mask.constData()), alpha.size.width(), alpha.size.height(), alpha.size.width(), QImage::Format_Grayscale8).copy();
  if (coverage.size() != base.size()) coverage = coverage.scaled(base.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  for (int y = 0; y < base.height(); ++y) {
    auto* pixels = reinterpret_cast<QRgbaFloat32*>(base.scanLine(y));
    for (int x = 0; x < base.width(); ++x) {
      const auto weight = static_cast<float>(coverage.constScanLine(y)[x]) / 255.0F;
      const auto pixel = pixels[x];
      pixels[x] = {pixel.red() * weight, pixel.green() * weight, pixel.blue() * weight, pixel.alpha() * weight};
    }
  }
  return base;
}
QImage trimmed(QImage image) {
  int left = image.width(), right = -1, top = image.height(), bottom = -1;
  for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
    if (image.pixelColor(x, y).alphaF() > 0) {
      left = std::min(left, x); right = std::max(right, x);
      top = std::min(top, y); bottom = std::max(bottom, y);
    }
  return image.copy(QRect(QPoint(left, top), QPoint(right, bottom)));
}
}
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  try {
    QTemporaryDir temporary;
    QImage source(160, 100, QImage::Format_RGB32); source.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x)
      source.setPixelColor(x, y, QColor(x, y * 2, 170));
    const auto path = temporary.filePath("source.png"); require(source.save(path), "Save source fixture");
    EditorPreviewController editor;
    editor.loadAsset("cutout-export", path, {}, {}, {}, {}, {}); wait(editor);
    editor.addMask("rectangle"); editor.setActiveMaskRadiusX(.28); editor.setActiveMaskRadiusY(.25);
    require(editor.setSelectionEdgeAdjustment("featherPixels", 3.0), "Set feather in source pixels");
    require(std::abs(editor.selectionEdgeControls().value("featherPixels").toDouble() - 3.0) < 1e-9, "Pixel feather must round trip");
    wait(editor);
    const auto before = editor.currentAssetBoundAdjustmentPatchJson();
    const auto history = editor.editHistoryCurrentIndex();
    const auto output = temporary.filePath("export.png");
    require(!editor.startSelectionCutoutExport(-1, path), "Never overwrite source");
    require(!editor.startSelectionCutoutExport(-1, temporary.filePath("wrong.jpg")), "Cutout export must preserve alpha in PNG");
    require(editor.startSelectionCutoutExport(-1, output), "Export working selection without creating a layer"); wait(editor);
    require(editor.photoLayerCount() == 0 && editor.currentAssetBoundAdjustmentPatchJson() == before && editor.editHistoryCurrentIndex() == history,
        "Export must leave layers, selection and history untouched");
    require(editor.copyPixelsFromSelection(-1, true), "Create independent cutout"); wait(editor);
    const auto raster = rasterize_photo_paint_layer(editor.photoLayers().first().toMap());
    const auto expected = trimmed(raster.convertedToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA64));
    require(QImage(output).convertToFormat(QImage::Format_RGBA64) == expected, "Export-only pixels must match materialized cutout, trimmed exactly");
    QElapsedTimer preview_wait; preview_wait.start();
    while (editor.cutoutPreview().value("source").toString().isEmpty() && preview_wait.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1);
    }
    const auto preview = editor.cutoutPreview();
    require(preview.value("layerId") == editor.photoLayers().first().toMap().value("id"), "Preview must identify the new layer");
    const auto encoded = preview.value("source").toString().section(',', 1).toLatin1();
    const auto preview_image = QImage::fromData(QByteArray::fromBase64(encoded));
    require(preview_image.convertToFormat(QImage::Format_RGBA8888) == raster.convertedToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888), "Bounded preview must preserve display color and alpha");
    require(editor.deselectWorkingSelection(), "Deselect after creating the cutout"); wait(editor);
    const auto ready_output = temporary.filePath("ready.png");
    require(editor.startSelectionCutoutExport(0, ready_output, true), "Export the displayed cutout"); wait(editor);
    require(QImage(ready_output).convertToFormat(QImage::Format_RGBA64) == expected, "Displayed cutout export must not apply feather twice");
    const auto created = editor.currentAssetBoundAdjustmentPatchJson();
    editor.dismissCutoutPreview();
    require(editor.cutoutPreview().isEmpty() && editor.currentAssetBoundAdjustmentPatchJson() == created, "Returning to composition must not alter recipe");
    require(QImage(path) == source, "Original source must remain unchanged");
    require(editor.reselectWorkingSelection() && editor.copyPixelsFromSelection(-1, true), "Prepare a second result preview"); wait(editor);
    require(!editor.cutoutPreview().isEmpty() && editor.setPhotoLayerOpacity(0, .75) && editor.cutoutPreview().isEmpty(),
        "Changing the displayed layer must dismiss its stale result preview"); wait(editor);
    for (const auto* kind : {"rectangle", "ellipse", "polygon", "luminance"}) {
      EditorPreviewController sample;
      sample.loadAsset(QStringLiteral("cutout-parity-") + kind, path, {}, {}, {}, {}, {}); wait(sample);
      sample.addMask(kind); sample.setActiveMaskFeather(.06); wait(sample);
      auto state = *parse_adjustment_patch_json(sample.currentAssetBoundAdjustmentPatchJson()).state;
      auto masks = sample.workingSelectionMasks();
      for (int variant = 0; variant < 3; ++variant) {
        auto current = state; auto coverage = masks;
        if (variant == 1) { auto mask = coverage.first().toMap(); mask.insert("selectionFineEdge", 40.0); mask.insert("refineLuminanceMin", .2); coverage[0] = mask; }
        if (variant == 2) { current.crop_rect_x = .1; current.crop_rect_y = .15; current.crop_rect_width = .6; current.crop_rect_height = .65; current.crop_preset = "Custom"; }
        require(render_base_selection_cutout(path, current, coverage) == reference_cutout(path, current, coverage),
            "Cutout must match canonical pixel output for geometry, source-sensitive refinements and crops");
      }
    }

    QByteArray jpeg; QBuffer jpeg_buffer(&jpeg); jpeg_buffer.open(QIODevice::WriteOnly);
    require(source.save(&jpeg_buffer, "JPEG"), "Prepare oriented JPEG fixture"); jpeg_buffer.close();
    const auto exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
    const auto oriented = jpeg.left(2) + QByteArray::fromHex("ffe10022") + exif + jpeg.mid(2);
    const auto oriented_path = temporary.filePath("oriented.jpg"); QFile oriented_file(oriented_path);
    require(oriented_file.open(QIODevice::WriteOnly) && oriented_file.write(oriented) == oriented.size(), "Write orientation metadata"); oriented_file.close();
    QImageReader oriented_reader(oriented_path);
    require(oriented_reader.transformation() != QImageIOHandler::TransformationNone, "JPEG fixture must actually be oriented");
    EditorPreviewController oriented_editor; oriented_editor.loadAsset("oriented-cutout", oriented_path, {}, {}, {}, {}, {}); wait(oriented_editor);
    oriented_editor.addMask("rectangle"); wait(oriented_editor);
    const auto oriented_state = *parse_adjustment_patch_json(oriented_editor.currentAssetBoundAdjustmentPatchJson()).state;
    require(render_base_selection_cutout(oriented_path, oriented_state, oriented_editor.workingSelectionMasks()) ==
        reference_cutout(oriented_path, oriented_state, oriented_editor.workingSelectionMasks()), "Oriented sources must retain exact canonical coverage");
    std::cout << "pixel feather, export-only PNG, trimmed exact alpha/color, preview identity, no double feather, source/history preservation passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
