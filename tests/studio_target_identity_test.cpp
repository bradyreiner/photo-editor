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
                           "selectedEditTargetSourceIndex"}) {
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
    true;
  )JS"));
  if (result.isError() || !result.toBool()) {
    std::cerr << result.toString().toStdString() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
