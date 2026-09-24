#pragma once

#include "edit_recipe_state.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <QString>

namespace photo_editor::desktop {

struct DecoderProvenanceResult final {
  QString provenance;
  QString error;
  bool raw_source = false;

  [[nodiscard]] bool ok() const noexcept {
    return !provenance.trimmed().isEmpty() &&
           error.isEmpty();
  }
};

[[nodiscard]] DecoderProvenanceResult
resolve_decoder_provenance_for_source(
    const QString& source_path,
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata*
        metadata = nullptr);

}  // namespace photo_editor::desktop
