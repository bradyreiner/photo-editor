#include "layer_session.h"

#include <cstdlib>
#include <iostream>

#include <QVariantMap>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  photo_editor::desktop::LayerSession session;
  const QVariantList layers{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("layer-a")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("layer-b")}},
  };
  if (!session.replaceLayers(layers) || session.replaceLayers(layers) ||
      session.layerCount() != 2 || session.layers() != layers) {
    return fail("LayerSession must atomically own layer replacement and identity");
  }

  session.appendVisibilityState(QVariantMap{
      {QStringLiteral("name"), QStringLiteral("Only subject")},
      {QStringLiteral("layers"), QVariantList{
           QVariantMap{{QStringLiteral("id"), QStringLiteral("layer-a")},
                       {QStringLiteral("enabled"), true}},
           QVariantMap{{QStringLiteral("id"), QStringLiteral("layer-b")},
                       {QStringLiteral("enabled"), false}},
       }},
  });
  if (session.visibilityStateCount() != 1 ||
      session.visibilityStates().front().toMap()
              .value(QStringLiteral("name")).toString() !=
          QStringLiteral("Only subject") ||
      session.removeVisibilityState(-1) ||
      !session.removeVisibilityState(0) ||
      session.visibilityStateCount() != 0) {
    return fail("saved layer visibility state ownership must be ordered and bounded");
  }

  const QVariantMap adjustments{
      {QStringLiteral("exposure"), 1.25},
      {QStringLiteral("contrast"), -4.0},
  };
  if (!session.copyAdjustments(adjustments) ||
      session.copyAdjustments(adjustments) ||
      !session.hasCopiedAdjustments() ||
      session.copiedAdjustments() != adjustments ||
      !session.clearCopiedAdjustments() ||
      session.hasCopiedAdjustments() ||
      !session.copiedAdjustments().isEmpty() ||
      session.clearCopiedAdjustments()) {
    return fail("layer adjustment clipboard replacement and clearing must be atomic");
  }

  if (!session.replaceLayers({}) || !session.layers().isEmpty() ||
      session.layerCount() != 0) {
    return fail("clearing layer state must publish an empty session atomically");
  }

  return EXIT_SUCCESS;
}
