#pragma once
#include <QString>
#include <QVariantList>
namespace photo_editor::desktop {
struct ExpandedSmartDocuments {
  QVariantList layers;
  QVariantList masks;
  QString error;
};
// Expansion keeps child content in the existing working-space compositor.
// Each instance receives private layer/mask identities, so shared authoring
// definitions never collide when a component is placed more than once.
ExpandedSmartDocuments expand_smart_documents(
    const QVariantList& layers, const QVariantList& masks,
    double document_layer_style_light_angle = -45.0);
}
