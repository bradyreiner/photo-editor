#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QFile file(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  if (!file.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
  const auto source = QString::fromUtf8(file.readAll());
  QJSEngine engine;
  engine.evaluate(QStringLiteral(
      "var root = {selectedEditTarget: {layerId:'b', layerIndex:1, componentId:'m', sourceIndex:0, maskSetId:'s'}};"
      "var editorPreview = {photoLayerCount:2, maskStack:[{id:'m',maskSetId:'s'}]};"
      "root.photoLayerRows = [{id:'a'},{id:'b'}];"));
  // Execute the production functions, not a duplicated model of their logic.
  for (const auto* name : {"photoLayerIdentityAt", "photoLayerIndexForIdentity",
                           "selectedEditTargetLayerIndex", "selectedEditTargetMaskSetId",
                           "selectedEditTargetSourceIndex",
                           "photoLayerHasDirectTransform", "editDocumentBreadcrumbText"}) {
    const auto marker = QStringLiteral("    function ") + QString::fromLatin1(name) + '(';
    const auto start = source.indexOf(marker);
    const auto end = source.indexOf(QStringLiteral("\n    }"), start);
    if (start < 0 || end < 0) return EXIT_FAILURE;
    const auto result = engine.evaluate(QStringLiteral("root.") + QString::fromLatin1(name) +
        QStringLiteral(" = ") + source.mid(start, end + 6 - start).trimmed());
    if (result.isError()) {
      std::cerr << result.toString().toStdString() << '\n';
      return EXIT_FAILURE;
    }
  }
  const auto result = engine.evaluate(QStringLiteral(R"JS(
    root.editBoardMode = false;
    if (root.editDocumentBreadcrumbText() !== '')
      throw Error('standalone photo gained board context');
    root.editBoardMode = true;
    root.editBoardDocumentName = 'Afterhours';
    root.editBoardIndex = 1;
    root.activeEditBoard = {boardName:'B - Night Transmission',title:'02'};
    if (root.editDocumentBreadcrumbText() !== 'Afterhours / B - Night Transmission')
      throw Error('document breadcrumb used sequence number instead of board name');
    root.editBoardDocumentName = '';
    if (root.editDocumentBreadcrumbText() !== 'B - Night Transmission')
      throw Error('unnamed document lost the active board');
    if (root.selectedEditTargetLayerIndex() !== 1 || root.selectedEditTargetSourceIndex() !== 0)
      throw Error('initial target or component zero');
    root.photoLayerRows.reverse();
    editorPreview.maskStack.unshift({id:'other',maskSetId:'s'});
    if (root.selectedEditTargetLayerIndex() !== 0 || root.selectedEditTargetSourceIndex() !== 1)
      throw Error('reorder changed target identity');
    root.photoLayerRows = [{id:'a'},{id:'replacement'}];
    editorPreview.maskStack = [{id:'other',maskSetId:'s'}];
    if (root.selectedEditTargetLayerIndex() !== -1 || root.selectedEditTargetSourceIndex() !== -1)
      throw Error('deleted target resolved to a neighbor');
    root.selectedEditTarget = {sourceIndex:0};
    if (root.selectedEditTargetSourceIndex() !== 0)
      throw Error('legacy component zero was lost');
    if (!root.photoLayerHasDirectTransform({kind:'group'}) ||
        !root.photoLayerHasDirectTransform({kind:'pixel',pixelWidth:120,pixelHeight:80}) ||
        root.photoLayerHasDirectTransform({kind:'fill',pixelWidth:120,pixelHeight:80}))
      throw Error('group and raster transform targets');
    true;
  )JS"));
  if (result.isError() || !result.toBool()) {
    std::cerr << result.toString().toStdString() << '\n';
    return EXIT_FAILURE;
  }
  if (!source.contains(QStringLiteral(
          "? root.sourceCanvasWidthForLayers(layerData)")) ||
      !source.contains(QStringLiteral(
          "? root.sourceCanvasHeightForLayers(layerData)"))) {
    std::cerr << "Group transforms must use the full composited canvas bounds.\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
