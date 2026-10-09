#include "mask_edit_normalization.h"
#include "mask_session.h"

#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    auto brush = default_mask_definition(QStringLiteral("brush"));
    QVariantList points;
    for (int i = 0; i < 1000; ++i) {
      points.append(QVariantMap{{"x", double(i % 100) / 100.0}, {"y", 0.3},
          {"size", 0.07}, {"flow", 0.6}, {"pressure", 0.4}});
    }
    brush.insert("brushPoints", points);
    brush.insert("segmentationWidth", 4);
    brush.insert("segmentationHeight", 4);
    brush.insert("segmentationData", QByteArray(16, '\x7f'));
    const auto previous = normalized_mask_definition(brush, 1);
    const QVariantMap changes{{"opacity", 0.3}, {"density", 0.4}, {"inverted", true},
        {"feather", 0.5}, {"exposure", 0.8}, {"brushSize", 0.2}, {"brushFlow", 0.2},
        {"brushPressureSize", 0.6}, {"brushPressureFlow", 0.8}, {"name", "Renamed"},
        {"toolRole", "retouch"}, {"retouchMode", "clone"}, {"operation", "subtract"}};
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
      auto edited = previous;
      edited.insert(it.key(), it.value());
      const auto normalized = normalized_mask_edit_definition(edited, previous, 1);
      if (normalized != normalized_mask_definition(edited, 1))
        throw std::runtime_error(("Scalar normalization changed: " + it.key()).toStdString());
      if (normalized.value("brushPoints") != previous.value("brushPoints") ||
          normalized.value("brushStrokes") != previous.value("brushStrokes") ||
          normalized.value("segmentationData") != previous.value("segmentationData"))
        throw std::runtime_error("Scalar edit changed existing brush or raster geometry");
    }
    for (const auto* field : {"brushPoints", "brushStrokes", "segmentationData", "kind"}) {
      auto edited = previous;
      if (QString::fromLatin1(field) == "brushPoints") {
        auto changed = edited.value(field).toList();
        changed[0] = QVariantMap{{"x", -5.0}, {"y", 2.0}, {"size", 0.1}};
        edited.insert(field, changed);
      } else if (QString::fromLatin1(field) == "brushStrokes") {
        auto strokes = edited.value(field).toList();
        auto stroke = strokes[0].toMap();
        stroke.insert("samples", QVariantList{QVariantMap{{"x", -1.0}, {"y", 2.0}}});
        strokes[0] = stroke;
        edited.insert(field, strokes);
      } else if (QString::fromLatin1(field) == "kind") {
        edited.insert(field, "radial");
      } else {
        edited.insert(field, QByteArray(16, '\0'));
      }
      if (normalized_mask_edit_definition(edited, previous, 1) !=
          normalized_mask_definition(edited, 1))
        throw std::runtime_error("Changed geometry failed ordinary normalization parity");
    }
    auto second = previous;
    second.insert("id", "retouch-2");
    second.insert("toolRole", "retouch");
    second.insert("retouchMode", "clone");
    const auto stack = normalize_mask_stack({previous, second,
        default_mask_definition(QStringLiteral("radial"), 3)});
    for (const QVariantList& edited : {QVariantList{stack[1], stack[0]},
         QVariantList{stack[0], stack[2]},
         QVariantList{stack[0], default_mask_definition(QStringLiteral("brush"), 4)}}) {
      if (normalized_mask_edit_stack(edited, stack) != normalize_mask_stack(edited))
        throw std::runtime_error("Delete/reorder/new-mask normalization changed canonical output");
    }
    if (previous != normalized_mask_definition(brush, 1))
      throw std::runtime_error("Reuse changed the original immutable brush definition");
    MaskSession session;
    session.setAssetScope("asset-a");
    session.replaceStack({previous}, 0);
    QVariantMap payload{{"id", previous.value("id")}, {"brushPoints", previous.value("brushPoints")},
        {"brushStrokes", previous.value("brushStrokes")}, {"schema", "source-v1"}};
    QString signature;
    if (session.brushSourceSignatureLookup(payload, &signature))
      throw std::runtime_error("Signature cache trusted an uncomputed signature");
    session.storeBrushSourceSignature(payload, "computed-signature");
    if (!session.brushSourceSignatureLookup(payload, &signature) || signature != "computed-signature")
      throw std::runtime_error("Signature cache lost exact computed payload");
    auto changed = payload;
    changed.insert("schema", "different-source");
    if (session.brushSourceSignatureLookup(changed, &signature))
      throw std::runtime_error("Signature cache reused changed source identity");
    session.replaceStack({}, -1);
    if (session.brushSourceSignatureLookup(payload, &signature))
      throw std::runtime_error("Deleted mask retained a signature payload");
    session.storeBrushSourceSignature(payload, "computed-signature");
    session.setAssetScope("asset-b");
    if (session.brushSourceSignatureLookup(payload, &signature))
      throw std::runtime_error("Signature cache crossed asset boundaries");
    std::cout << "mask edit normalization parity passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
