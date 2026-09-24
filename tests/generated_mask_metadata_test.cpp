#include "generated_mask_metadata.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;
  const auto facts = GeneratedMaskMetadataFacts{
      .source_size = QSize(6000, 4000),
      .crop_x = 0.1,
      .crop_y = 0.2,
      .crop_width = 0.7,
      .crop_height = 0.6,
      .crop_zoom = 1.25,
      .transform_rotation = 2.5,
      .crop_preset = QStringLiteral("4x5"),
      .source_image_version = QStringLiteral("source-v3"),
      .runtime_label = QStringLiteral("Semantic Runtime"),
  };
  const auto metadata = make_generated_mask_metadata(
      QStringLiteral("person"), facts);
  const auto dependencies =
      metadata.value(QStringLiteral("dependencies")).toMap();
  if (metadata.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("people") ||
      metadata.value(QStringLiteral("sourceResolution")).toString() !=
          QStringLiteral("6000x4000") ||
      metadata.value(QStringLiteral("promptType")).toString() !=
          QStringLiteral("semantic:people") ||
      metadata.value(QStringLiteral("modelName")).toString() !=
          QStringLiteral("Semantic Runtime") ||
      dependencies.value(QStringLiteral("sourceImageVersion")).toString() !=
          QStringLiteral("source-v3") ||
      dependencies.value(QStringLiteral("coordinateSpaceContract")).toString().isEmpty()) {
    return fail("generated metadata should normalize kind and record source, model, prompt, and coordinate dependencies");
  }

  QVariantMap mask{{QStringLiteral("kind"), QStringLiteral("people")}};
  if (!stamp_generated_mask_metadata(&mask, facts) ||
      mask.value(QStringLiteral("generatedMaskMetadata")).toMap() != metadata ||
      mask.value(QStringLiteral("generatedMaskStale")).toBool() ||
      generated_mask_metadata_is_stale(mask, metadata)) {
    return fail("stamped metadata should be complete, current, and explicitly not stale");
  }
  auto changed = metadata;
  changed.insert(QStringLiteral("cropZoom"), 1.5);
  if (!generated_mask_metadata_is_stale(mask, changed)) {
    return fail("crop dependency changes should invalidate generated metadata");
  }
  changed = metadata;
  changed.insert(QStringLiteral("sourceImageVersion"), QStringLiteral("source-v4"));
  if (!generated_mask_metadata_is_stale(mask, changed)) {
    return fail("source revision changes should invalidate generated metadata");
  }
  changed = metadata;
  changed.insert(QStringLiteral("runtime"), QStringLiteral("Other Runtime"));
  if (!generated_mask_metadata_is_stale(mask, changed)) {
    return fail("runtime changes should invalidate generated metadata");
  }
  auto incomplete = mask;
  auto incomplete_metadata = metadata;
  incomplete_metadata.remove(QStringLiteral("dependencies"));
  incomplete.insert(QStringLiteral("generatedMaskMetadata"), incomplete_metadata);
  if (!generated_mask_metadata_is_stale(incomplete, metadata)) {
    return fail("missing required provenance should fail stale");
  }
  auto explicitly_stale = mask;
  explicitly_stale.insert(QStringLiteral("generatedMaskStale"), true);
  QVariantMap legacy_object{{QStringLiteral("kind"), QStringLiteral("object")}};
  QVariantMap selection{{QStringLiteral("kind"), QStringLiteral("linear")}};
  if (!generated_mask_metadata_is_stale(explicitly_stale, metadata) ||
      generated_mask_metadata_is_stale(legacy_object, metadata) ||
      generated_mask_metadata_is_stale(selection, metadata) ||
      stamp_generated_mask_metadata(&selection, facts) ||
      stamp_generated_mask_metadata(nullptr, facts)) {
    return fail("explicit stale state must win while legacy objects and non-generated masks retain fail-closed compatibility");
  }
  return EXIT_SUCCESS;
}
