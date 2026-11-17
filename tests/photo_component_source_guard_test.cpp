#include "editor_preview_controller.h"
#include "photo_component_workspace_controller.h"
#include "desktop_retouch_source.h"

#include <QCoreApplication>
#include <QColor>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QMetaProperty>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void settle(EditorPreviewController& editor, const char* message) {
  QElapsedTimer timer;
  timer.start();
  const auto idle = [&editor]() {
    return !editor.processing() && !editor.fullRenderRunning() && !editor.professionalToolsBusy() &&
        !editor.selectionWorkBusy() && !editor.photoPaintBusy() &&
        !editor.photoExportBusy();
  };
  while (!idle() && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(idle(), message);
}

template <typename Predicate>
void wait_until(Predicate done, const char* message) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(done(), message);
}

void test_component_source_guards(const QString& runtime_path) {
  QImage source(96, 64, QImage::Format_RGB32);
  source.fill(QColor(QStringLiteral("#58718c")));
  const auto source_path = runtime_path + QStringLiteral("/component-source.png");
  require(source.save(source_path, "PNG"), "Save the component guard fixture image.");

  EditorPreviewController parent;
  parent.loadAsset(QStringLiteral("component-source-guard-parent"), source_path,
                   {}, {}, {}, {}, {});
  settle(parent, "The parent image must load before creating a component.");
  require(parent.addFillLayer(QStringLiteral("#e47d3d"), QStringLiteral("Editable fill")),
          "Create a component source layer.");
  settle(parent, "The component source layer must finish rendering.");
  require(parent.photoLayerCount() > 0 && parent.convertPhotoLayerToComponent(0),
          "Convert the layer to an editable component.");
  settle(parent, "The parent component must finish converting.");

  PhotoComponentWorkspaceController workspace(&parent);
  require(workspace.open(0), "Open component contents in an isolated child editor.");
  wait_until([&workspace]() { return !workspace.busy() && workspace.active(); },
             "The component child must finish opening.");
  auto* child = workspace.controller();
  require(child != nullptr && child != &parent && child->hasAsset(),
          "Component content editing must use its transient child editor.");
  require(child->metaObject()->indexOfMethod("brushPresetNeedsSourceSample(QString)") >= 0,
          "Source-sampled preset availability must be callable by the real QML menus.");
  settle(*child, "The component child preview must finish loading.");

  const auto recipe_before_unsupported_actions = child->currentAssetBoundAdjustmentPatchJson();
  const auto history_before_unsupported_actions = child->editHistoryCurrentIndex();
  const auto masks_before_unsupported_actions = child->maskStack();

  child->removeDustSpots();
  child->removeRedEye();
  child->applySensorDustMap();
  require(!child->queueActiveFinalInpaintPatch() &&
              !child->regenerateActiveInpaintPatch() &&
              !child->reopenActiveRetouchSelection(),
          "Inpaint and reopening unavailable retouch selections must reject component source access.");
  child->addMask(QStringLiteral("depth"));
  require(!child->sampleActiveDepthRangeAt(0.5, 0.5) &&
              !child->selectColorMixerChannelAt(0.5, 0.5) &&
              !child->autoDetectSkinProtection(),
          "Depth and unsupported photographic samplers must reject component source access.");
  require(child->status().contains(QStringLiteral("Skin detection needs the underlying photo")),
          "Source-sampling operations must report why they were rejected.");
  child->setWhiteBalanceSampling(true);
  child->sampleWhiteBalanceAt(0.5, 0.5);
  child->setColorToolSamplingTarget(QStringLiteral("color-replace-source"));
  child->sampleColorToolAt(0.5, 0.5);
  require(child->maskStack() == masks_before_unsupported_actions &&
              child->currentAssetBoundAdjustmentPatchJson() == recipe_before_unsupported_actions &&
              child->editHistoryCurrentIndex() == history_before_unsupported_actions,
          "Rejected source-based actions must leave the child recipe, masks, and history unchanged.");

  const auto available_property_index =
      child->metaObject()->indexOfProperty("componentRetouchAvailable");
  const auto reason_property_index =
      child->metaObject()->indexOfProperty("componentRetouchCapabilityReason");
  require(available_property_index >= 0 && reason_property_index >= 0,
          "Component retouch capability must be exposed to the UI.");
  const auto available_property = child->metaObject()->property(available_property_index);
  const auto reason_property = child->metaObject()->property(reason_property_index);
  require(available_property.isReadable() && !available_property.isWritable() &&
              reason_property.isReadable() && !reason_property.isWritable() &&
              child->componentRetouchAvailable() &&
              child->componentRetouchCapabilityReason().isEmpty() &&
              available_property.read(child).toBool() &&
              reason_property.read(child).toString().isEmpty(),
          "A valid bounded component canvas must advertise retouch as a read-only capability.");

  const auto parent_recipe_before_retouch_drafts = parent.currentAssetBoundAdjustmentPatchJson();
  const auto parent_masks_before_retouch_drafts = parent.maskStack();
  const auto parent_history_before_retouch_drafts = parent.editHistoryCurrentIndex();
  struct RetouchTool {
    const char* mode;
    void (EditorPreviewController::*start)();
  };
  const RetouchTool retouch_tools[] = {
      {"remove", &EditorPreviewController::startRemoveBrush},
      {"heal", &EditorPreviewController::startHealBrush},
      {"patch", &EditorPreviewController::startPatchBrush},
      {"clone", &EditorPreviewController::startCloneBrush},
  };
  for (const auto& tool : retouch_tools) {
    const auto draft_recipe_before = child->currentAssetBoundAdjustmentPatchJson();
    const auto draft_masks_before = child->maskStack();
    (child->*tool.start)();
    settle(*child, "A supported component retouch tool must open without a source-photo worker.");
    require(child->hasActiveMask() && child->activeMaskToolRole() == QStringLiteral("retouch") &&
                child->activeMaskRetouchMode() == QString::fromLatin1(tool.mode) &&
                !child->activeMaskCommitted(),
            "Remove, heal, patch, and clone must open as editable repair drafts in a valid component.");
    const auto draft = child->maskStack().value(child->activeMaskIndex()).toMap();
    require(draft.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() ==
                    kComponentPostCompositeRetouchVersion &&
                draft.value(QStringLiteral("componentRetouchCanvasWidth")).toInt() ==
                    child->sourceImageWidth() &&
                draft.value(QStringLiteral("componentRetouchCanvasHeight")).toInt() ==
                    child->sourceImageHeight(),
            "Component retouch drafts must record the versioned post-composite canvas basis.");

    child->discardActiveRetouch();
    settle(*child, "Discarding an uncommitted component repair must finish cleanly.");
    require(child->maskStack() == draft_masks_before &&
                child->currentAssetBoundAdjustmentPatchJson() == draft_recipe_before,
            "Canceling each supported retouch tool must restore the exact child recipe and masks.");
    require(parent.currentAssetBoundAdjustmentPatchJson() == parent_recipe_before_retouch_drafts &&
                parent.maskStack() == parent_masks_before_retouch_drafts &&
                parent.editHistoryCurrentIndex() == parent_history_before_retouch_drafts,
            "Opening and canceling a component retouch draft must not mutate its parent document.");
  }

  child->startSelectionBrush(false);
  settle(*child, "The ordinary freehand selection brush must be ready.");
  require(child->hasActiveMask() && child->activeMaskKind() == QStringLiteral("brush") &&
              !child->activeMaskBrushAutoMask() && !child->activeMaskBrushProtectEdges(),
          "The ordinary freehand selection brush must remain available.");
  const auto recipe_before_brush_guards = child->currentAssetBoundAdjustmentPatchJson();
  const auto history_before_brush_guards = child->editHistoryCurrentIndex();
  child->setActiveMaskBrushAutoMask(true);
  child->setActiveMaskBrushProtectEdges(true);
  require(child->brushPresetNeedsSourceSample(QStringLiteral("Edge Aware")) &&
              child->brushPresetNeedsSourceSample(QStringLiteral("Hair Refine")) &&
              !child->brushPresetNeedsSourceSample(QStringLiteral("Soft Round")) &&
              !child->applyActiveMaskBrushPreset(QStringLiteral("Edge Aware")) &&
              !child->activeMaskBrushAutoMask() && !child->activeMaskBrushProtectEdges() &&
              child->status().contains(QStringLiteral("Source-sampled brush settings are unavailable")) &&
              child->currentAssetBoundAdjustmentPatchJson() == recipe_before_brush_guards &&
              child->editHistoryCurrentIndex() == history_before_brush_guards,
          "Auto Mask, Protect Edges, and source-sampled presets must fail unchanged.");

  child->beginActiveBrushStroke();
  require(child->brushStrokeActive(),
          "A freehand brush stroke must still start on component artwork.");
  child->addBrushPoint(0.42, 0.48);
  child->addBrushPoint(0.58, 0.52);
  child->endActiveBrushStroke();
  settle(*child, "The freehand component selection stroke must finish committing.");
  require(child->maskStack().value(child->activeMaskIndex()).toMap().value(QStringLiteral("brushStrokes")).toList().size() > 0,
          "The freehand component selection stroke must remain editable after commit.");

  require(workspace.cancel(), "Cancel the transient component guard fixture.");
  wait_until([&workspace]() { return !workspace.busy() && !workspace.active(); },
             "Cancel must close the component guard fixture.");
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyComponentSourceGuardTests"));
  QCoreApplication::setApplicationName(QStringLiteral("PhotoComponentSourceGuards"));
  QGuiApplication application(argc, argv);
  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(60000); }
  } drain_workers;
  try {
    test_component_source_guards(runtime.path());
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "photo_component_source_guard_test passed\n";
  return 0;
}
