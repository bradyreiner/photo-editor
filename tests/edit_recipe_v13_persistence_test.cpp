#include "catalog_recipe_projection.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_v13_persistence.h"
#include "desktop_recipe_persistence_service.h"
#include "editor_recipe_persistence_launcher.h"
#include "photo_layer_effect_codec.h"
#include "photo_pixel_stroke.h"

#include "photo_editor/pipeline/edit_recipe_v13.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool prepared_recipe_payload_contracts(
    const photo_editor::desktop::EditRecipeState& canonical_state,
    const QString& app_data_root) {
  using namespace photo_editor::desktop;
  const QString asset_id = QStringLiteral("asset:prepared-payload");
  const DecoderProvenanceResult provenance{
      .provenance = QStringLiteral("raster:test-v1"),
      .raw_source = false,
  };
  const auto recipe_asset_root =
      QDir(app_data_root).filePath(QStringLiteral("recipe-assets"));

  auto legacy_state = canonical_state;
  legacy_state.photographic_rendering_model = QStringLiteral("legacy-oklch-v1");
  const auto legacy_service = prepare_desktop_recipe_persistence(
      asset_id, legacy_state, provenance, recipe_asset_root);
  const auto legacy_prepared = prepare_editor_recipe_persistence_payload(
      asset_id, legacy_state, provenance, {}, app_data_root, false);
  const QStringList legacy_arguments{
      QStringLiteral("--legacy-v1"), asset_id,
      legacy_service.legacy_recipe_json};
  if (!legacy_service.ok() || !legacy_service.legacy_only ||
      !legacy_prepared.ok() || legacy_prepared.migration_acceptance ||
      legacy_prepared.payload != catalog_helper_argument_payload(legacy_arguments)) {
    return false;
  }

  const auto canonical_service = prepare_desktop_recipe_persistence(
      asset_id, canonical_state, provenance, recipe_asset_root);
  const auto canonical_prepared = prepare_editor_recipe_persistence_payload(
      asset_id, canonical_state, provenance, {}, app_data_root, false);
  const QStringList canonical_arguments{
      asset_id,
      canonical_service.legacy_recipe_json,
      canonical_service.canonical_document.recipe_payload,
      canonical_service.canonical_document.cache_key,
      canonical_service.canonical_document.provenance_payload,
  };
  if (!canonical_service.ok() || canonical_service.legacy_only ||
      !canonical_prepared.ok() || canonical_prepared.migration_acceptance ||
      canonical_prepared.payload != catalog_helper_argument_payload(canonical_arguments)) {
    return false;
  }

  const QVariantMap migration_evidence{
      {QStringLiteral("planCacheKey"),
       QString::fromUtf8("plan \"quoted\" \\ path α\n")},
      {QStringLiteral("evidenceSha256"), QString(64, QLatin1Char('a'))},
      {QStringLiteral("referenceModel"),
       QString::fromUtf8("reference \"model\" α\n")},
      {QStringLiteral("candidateModel"),
       QString::fromUtf8("candidate \\ renderer")},
      {QStringLiteral("approvalTime"),
       QString::fromUtf8("approved \"today\" α")},
      {QStringLiteral("auditNote"),
       QString::fromUtf8("metadata \"escape\" \\ α\n")},
  };
  const auto migration_prepared = prepare_editor_recipe_persistence_payload(
      asset_id, canonical_state, provenance, migration_evidence,
      app_data_root, true);
  QJsonArray profiles;
  for (const auto& identity : canonical_v13_profile_identities(
           canonical_service.canonical_document)) {
    profiles.append(identity);
  }
  const QStringList migration_arguments{
      asset_id,
      canonical_service.legacy_recipe_json,
      canonical_service.canonical_document.recipe_payload,
      canonical_service.canonical_document.cache_key,
      canonical_service.canonical_document.provenance_payload,
      QStringLiteral("12"),
      migration_evidence.value(QStringLiteral("referenceModel")).toString(),
      migration_evidence.value(QStringLiteral("candidateModel")).toString(),
      QString::fromUtf8(QJsonDocument(profiles).toJson(QJsonDocument::Compact)),
      migration_evidence.value(QStringLiteral("planCacheKey")).toString(),
      migration_evidence.value(QStringLiteral("evidenceSha256")).toString(),
      migration_evidence.value(QStringLiteral("approvalTime")).toString(),
      QString::fromUtf8(
          canonical_rendering_migration_evidence_payload(migration_evidence)),
  };
  const auto decoded_arguments = QJsonDocument::fromJson(
      migration_prepared.payload).object().value(QStringLiteral("arguments")).toArray();
  if (!migration_prepared.ok() || !migration_prepared.migration_acceptance ||
      migration_prepared.payload != catalog_helper_argument_payload(migration_arguments) ||
      decoded_arguments != QJsonArray::fromStringList(migration_arguments)) {
    return false;
  }

  auto malformed_evidence = migration_evidence;
  malformed_evidence.insert(QStringLiteral("evidenceSha256"), QStringLiteral("short"));
  const auto malformed_prepared = prepare_editor_recipe_persistence_payload(
      asset_id, canonical_state, provenance, malformed_evidence,
      app_data_root, true);
  const auto legacy_migration = prepare_editor_recipe_persistence_payload(
      asset_id, legacy_state, provenance, migration_evidence,
      app_data_root, true);
  return !malformed_prepared.ok() && malformed_prepared.payload.isEmpty() &&
         malformed_prepared.error == QStringLiteral("Measured migration evidence is incomplete.") &&
         !legacy_migration.ok() && legacy_migration.payload.isEmpty() &&
         legacy_migration.error ==
             QStringLiteral("Rendering migration requires a canonical target recipe.");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  if (app.arguments().size() == 3 && app.arguments().at(1) == "--checkpoint") {
    QFile file(app.arguments().at(2));
    if (!file.open(QIODevice::ReadOnly)) return fail("cannot read checkpoint copy");
    const auto payload = QJsonDocument::fromJson(file.readAll()).object().value("payload").toObject();
    auto records = payload.value("pending").toArray();
    for (const auto key : {"active", "retryMigration"})
      if (payload.value(key).isObject()) records.append(payload.value(key));
    QTemporaryDir assets;
    for (const auto& entry : records) {
      const auto object = entry.toObject();
      const auto recipe = QString::fromUtf8(QJsonDocument(object.value("recipe").toObject()).toJson(QJsonDocument::Compact));
      const auto state = photo_editor::desktop::parse_edit_recipe_json(recipe);
      const auto prepared = photo_editor::desktop::prepare_desktop_recipe_persistence(
          object.value("assetId").toString(), state, {}, assets.path());
      if (!prepared.ok() || !prepared.legacy_only ||
          QJsonDocument::fromJson(prepared.legacy_recipe_json.toUtf8()).object() != object.value("recipe").toObject())
        return fail("pending legacy snapshot did not preserve exact stored intent");
    }
    std::cout << "Exact legacy save preparation passed for " << records.size() << " recovered snapshots\n";
    return EXIT_SUCCESS;
  }
  photo_editor::desktop::EditRecipeState state;
  state.exposure = 1.25;
  state.highlights = -24.0;
  state.shadows = 18.0;
  state.vibrance = 12.0;
  state.renderer_transition_look =
      QStringLiteral("preserve-legacy-direct-luminance-v1");
  QTemporaryDir prepared_payload_root;
  if (!prepared_payload_root.isValid() ||
      !prepared_recipe_payload_contracts(state, prepared_payload_root.path())) {
    return fail("prepared legacy/canonical/migration payload bytes must match the synchronous contract");
  }
  state.preset_applications = {
      QVariantMap{
          {QStringLiteral("schema"),
           QStringLiteral(
               "photo-editor-reversible-preset-application:v1")},
          {QStringLiteral("presetId"),
           QStringLiteral("clean-portrait")},
          {QStringLiteral("beforePatchSha256"),
           QString(64, QLatin1Char('a'))},
          {QStringLiteral("afterPatchSha256"),
           QString(64, QLatin1Char('b'))},
      },
  };

  photo_editor::desktop::DesktopRecipeV13Context context;
  context.recipe_id = QStringLiteral("recipe:asset-42");
  context.source_id = QStringLiteral("asset:42");
  context.decoder_provenance = QStringLiteral("raster:test-v1");
  context.raw_source = false;

  const auto written =
      photo_editor::desktop::
          serialize_desktop_recipe_as_canonical_v13(
              state,
              context);
  if (!written.ok()) {
    return fail("representable desktop state should produce a canonical v13 document");
  }
  photo_editor::pipeline::RecipeError decode_error;
  const auto decoded =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          written.document.recipe_payload.toStdString(),
          &decode_error);
  if (!decoded.has_value() ||
      decoded->recipe_version !=
          photo_editor::pipeline::kEditRecipeV13Version ||
      decoded->recipe_id.value !=
          "recipe:asset-42" ||
      decoded->input.source.value != "asset:42" ||
      decoded->scene.exposure_ev != 1.25 ||
      decoded->tone.highlights != -0.24 ||
      decoded->tone.shadows != 0.18 ||
      decoded->look.vibrance != 0.12 ||
      decoded->output.renderer_transition_look !=
          photo_editor::pipeline::RendererTransitionLookV13::
              PreserveLegacyDirectLuminanceV1) {
    return fail("canonical v13 serialization must preserve identity and adjustment intent");
  }
  if (written.document.cache_key !=
      QString::fromStdString(
          photo_editor::pipeline::
              edit_recipe_v13_cache_key(*decoded))) {
    return fail("canonical persistence must publish the normalized v13 cache identity");
  }
  if (!written.document.provenance_payload.contains(
          QStringLiteral("photo-editor-edit-recipe-provenance:v2")) ||
      !written.document.provenance_payload.contains(
          QStringLiteral("clean-portrait"))) {
    return fail("canonical persistence must publish versioned preset audit provenance");
  }
  auto alternate_provenance_state = state;
  alternate_provenance_state.preset_applications.push_back(
      QVariantMap{
          {QStringLiteral("presetId"),
           QStringLiteral("landscape-pop")},
      });
  const auto alternate_provenance =
      photo_editor::desktop::
          serialize_desktop_recipe_as_canonical_v13(
              alternate_provenance_state,
              context);
  if (!alternate_provenance.ok() ||
      alternate_provenance.document.recipe_payload !=
          written.document.recipe_payload ||
      alternate_provenance.document.cache_key !=
          written.document.cache_key ||
      alternate_provenance.document.provenance_payload ==
          written.document.provenance_payload) {
    return fail("preset audit provenance must survive without changing render or cache identity");
  }
  const auto projected =
      photo_editor::desktop::
          project_canonical_v13_to_desktop_state(
              *decoded);
  if (!projected.ok() ||
      projected.state->exposure != 1.25 ||
      projected.state->highlights != -24.0 ||
      projected.state->shadows != 18.0 ||
      projected.state->vibrance != 12.0 ||
      projected.state->photographic_rendering_model !=
          QStringLiteral("aces2-ocio-v1") ||
      projected.state->renderer_transition_look !=
          QStringLiteral("preserve-legacy-direct-luminance-v1")) {
    return fail("asset-free canonical v13 intent must project back into desktop edit state");
  }
  const auto rewritten =
      photo_editor::desktop::
          serialize_desktop_recipe_as_canonical_v13(
              *projected.state,
              context);
  if (projected.state->crop_rect_width != 0.0 ||
      projected.state->crop_rect_height != 0.0) {
    return fail("neutral canonical geometry must not invent an explicit desktop crop");
  }
  if (!rewritten.ok() ||
      rewritten.document.recipe_payload !=
          written.document.recipe_payload ||
      rewritten.document.cache_key !=
          written.document.cache_key) {
    return fail("canonical v13 projection and rewrite must be byte-identical");
  }

  {
    auto legacy = state;
    legacy.photographic_rendering_model = QStringLiteral("legacy-oklch-v1");
    const auto saved = photo_editor::desktop::prepare_desktop_recipe_persistence(
        "legacy-asset", legacy, {}, {});
    if (!saved.ok() || !saved.legacy_only ||
        !saved.canonical_document.recipe_payload.isEmpty() ||
        saved.legacy_recipe_json != photo_editor::desktop::serialize_edit_recipe_json(legacy))
      return fail("legacy persistence must retain the exact recipe without renderer migration");
  }

  auto asset_bound_recipe = *decoded;
  asset_bound_recipe.masks.push_back(
      photo_editor::pipeline::MaskSetRecipeV13{
          .id =
              photo_editor::pipeline::AssetIdentifier{
                  "mask:set-1"},
      });
  const auto rejected_asset_projection =
      photo_editor::desktop::
          project_canonical_v13_to_desktop_state(
              asset_bound_recipe);
  if (rejected_asset_projection.ok() ||
      rejected_asset_projection.state.has_value()) {
    return fail("asset-backed v13 projection must fail closed until a resolver is supplied");
  }

  QTemporaryDir asset_root;
  if (!asset_root.isValid()) {
    return fail("canonical persistence test requires an asset-store root");
  }
  auto asset_backed_state = state;
  asset_backed_state.output_color_space = QStringLiteral("Display P3");
  asset_backed_state = photo_editor::desktop::append_named_recipe_snapshot(
      asset_backed_state, QStringLiteral("Before retouch"), QStringLiteral("review"));
  asset_backed_state.mask_stack = {
      QVariantMap{
          {QStringLiteral("id"),
           QStringLiteral("mask-source-1")},
          {QStringLiteral("maskSetId"),
           QStringLiteral("mask-set-1")},
          {QStringLiteral("kind"),
           QStringLiteral("brush")},
          {QStringLiteral("operation"),
           QStringLiteral("add")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("blendMode"),
           QStringLiteral("normal")},
          {QStringLiteral("segmentationData"),
           QByteArray("\0\x7f\xff", 3)},
          {QStringLiteral("width"), 3},
          {QStringLiteral("height"), 1},
      },
  };
  asset_backed_state.layers = {
      QVariantMap{
          {QStringLiteral("id"),
           QStringLiteral("pixel-layer-1")},
          {QStringLiteral("kind"),
           QStringLiteral("pixel")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("blendMode"),
           QStringLiteral("normal")},
          {QStringLiteral("pixelData"),
           QByteArray("png-bytes")},
      },
      QVariantMap{
          {QStringLiteral("id"),
           QStringLiteral("adjustment-layer-1")},
          {QStringLiteral("kind"),
           QStringLiteral("adjustment")},
          {QStringLiteral("name"),
           QStringLiteral("Local tone")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("blendMode"),
           QStringLiteral("normal")},
          {QStringLiteral("adjustments"),
           QVariantMap{
               {QStringLiteral("exposure"), 0.4},
               {QStringLiteral("highlights"), -20.0},
               {QStringLiteral("lutStack"),
                QVariantList{
                    QVariantMap{
                        {QStringLiteral("enabled"), true},
                        {QStringLiteral("strength"), 65.0},
                        {QStringLiteral("name"),
                         QStringLiteral("Proof look")},
                        {QStringLiteral("table"),
                         QVariantList{0.0, 0.5, 1.0}},
                    },
                }},
           }},
      },
  };
  const photo_editor::desktop::RecipeAssetStore
      asset_store(asset_root.path());
  {
    photo_editor::pipeline::PhotoEffectInstance effect;
    effect.id = "persistent-blur";
    effect.gaussian.sigma = 12.5;
    auto layer = asset_backed_state.layers[0].toMap();
    layer.insert(QStringLiteral("effectStack"),
        *photo_editor::desktop::encode_photo_layer_effects(std::vector{effect}));
    asset_backed_state.layers[0] = layer;
  }
  {
    auto paint_state = state;
    QVariantMap layer{{"id","float-paint"},{"kind","pixel"},{"name","Paint"},
        {"pixelWidth",256},{"pixelHeight",128},
        {"paintTiles",photo_editor::desktop::empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
    photo_editor::desktop::PhotoPixelStroke stroke;
    stroke.color=QColor("#2080ff"); stroke.opacity=0.123456; stroke.diameter=24;
    if(!stroke.begin(layer)) return fail("Paint initialization");
    stroke.point({40,40},1); layer.insert("paintTiles",stroke.commit());
    paint_state.layers={layer};
    const auto saved=photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(paint_state,context,asset_store);
    if(!saved.ok()) return fail("Float paint must save to the canonical asset store");
    const auto restored=photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(saved.document,asset_store);
    if(!restored.ok() || restored.state->layers!=paint_state.layers)
      return fail("Canonical paint tile payload and ICC profile must survive exact reopen");
    photo_editor::desktop::PhotoPixelStroke next;
    if(!next.begin(layer)) return fail("Second canonical paint stroke");
    next.point({200,40},1); layer.insert("paintTiles",next.commit()); paint_state.layers={layer};
    const auto changed=photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(paint_state,context,asset_store);
    const auto prior=photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(saved.document,asset_store);
    if(!changed.ok() || changed.document.cache_key==saved.document.cache_key || !prior.ok() || prior.state->layers!=restored.state->layers)
      return fail("A new paint tile must change canonical identity without mutating the previous snapshot");
  }
  const auto asset_written =
      photo_editor::desktop::
          serialize_desktop_recipe_as_canonical_v13_with_assets(
              asset_backed_state,
              context,
              asset_store);
  if (!asset_written.ok() ||
      asset_written.stored_asset_count != 4) {
    return fail("asset-backed canonical persistence must externalize mask, layer, and LUT payloads");
  }
  const auto asset_recipe =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          asset_written.document.recipe_payload.toStdString(),
          &decode_error);
  if (!asset_recipe.has_value() ||
      asset_recipe->masks.size() != 1 ||
      asset_recipe->masks.front().sources.size() != 1 ||
      !asset_recipe->masks.front()
           .sources.front()
           .payload_asset.has_value() ||
      !asset_recipe->masks.front()
           .sources.front()
           .payload_asset->value.starts_with("sha256:")) {
    return fail("canonical mask recipes must reference stored SHA-256 payload assets");
  }
  const auto pixel_layer = std::find_if(
      asset_recipe->layers.begin(),
      asset_recipe->layers.end(),
      [](const auto& layer) {
        return layer.id.value == "pixel-layer-1";
      });
  if (pixel_layer == asset_recipe->layers.end() ||
      !pixel_layer->content_asset.has_value() ||
      !pixel_layer->content_asset->value.starts_with(
          "sha256:")) {
    return fail("canonical pixel layers must reference stored SHA-256 content assets");
  }
  const auto asset_projected =
      photo_editor::desktop::
          project_canonical_v13_to_desktop_state_with_assets(
              *asset_recipe,
              asset_store);
  if (!asset_projected.ok() ||
      asset_projected.state->mask_stack !=
          photo_editor::desktop::normalize_mask_stack(
              asset_backed_state.mask_stack) ||
      asset_projected.state->layers !=
          asset_backed_state.layers) {
    return fail("stored mask and pixel-layer assets must reopen into byte-identical desktop payloads");
  }
  {
    photo_editor::desktop::EditRecipeState text_state;
    const QVariantMap typography{{"version", 1}, {"family", "Helvetica"}, {"face", ""},
        {"size", 27.375}, {"weight", 650}, {"italic", true}, {"underline", true},
        {"tracking", 1.25}, {"lineHeight", 1.45}, {"baselineOffset", -2.5},
        {"color", "#40ff8040"}};
    text_state.layers = {QVariantMap{{"id", "canonical-text"}, {"kind", "text"},
        {"text", QString::fromUtf8("Café headline")}, {"textTypography", typography},
        {"pixelWidth", 400}, {"pixelHeight", 120}, {"enabled", true}}};
    const auto saved_text = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(
        text_state, context, asset_store);
    if (!saved_text.ok()) return fail("Styled text must save as canonical content assets");
    const auto restored_text = photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(
        saved_text.document, asset_store);
    if (!restored_text.ok() || restored_text.state->layers != text_state.layers)
      return fail("Canonical text projection must preserve exact typography and Unicode content");
    const auto desktop_json = photo_editor::desktop::serialize_edit_recipe_json(*restored_text.state);
    if (QJsonDocument::fromJson(desktop_json.toUtf8()).object().value("recipeVersion").toInt() != 15)
      return fail("Canonical restored typography must retain desktop version 15");
    auto changed_layer = text_state.layers[0].toMap();
    auto changed_style = typography;
    changed_style.insert("tracking", 2.25);
    changed_layer.insert("textTypography", changed_style);
    text_state.layers[0] = changed_layer;
    const auto changed_text = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(
        text_state, context, asset_store);
    if (!changed_text.ok() || changed_text.document.recipe_payload == saved_text.document.recipe_payload)
      return fail("Typography edits must change canonical content identity");
    const auto original_again = photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(
        saved_text.document, asset_store);
    if (!original_again.ok() || original_again.state->layers != restored_text.state->layers)
      return fail("Later typography saves must not mutate an earlier canonical snapshot");
  }
  for (const auto* mode : {"point", "paragraph"}) {
    photo_editor::desktop::EditRecipeState text_state;
    QVariantMap typography{{"version", 2}, {"mode", mode}, {"paragraphSpacing", 18.25},
        {"family", "Helvetica"}, {"face", ""}, {"size", 27.375}, {"weight", 400},
        {"italic", true}, {"underline", false}, {"tracking", 1.25}, {"lineHeight", 1.45},
        {"baselineOffset", -2.5}, {"color", "#ffeaa251"}};
    QVariantMap layer{{"id", "canonical-text-v2"}, {"kind", "text"},
        {"text", QString::fromUtf8("Café headline\nSecond paragraph")}, {"textTypography", typography},
        {"pixelWidth", 400}, {"pixelHeight", 120}, {"textOriginX", 1}, {"textOriginY", 2}, {"enabled", true}};
    text_state.layers = {layer};
    const auto saved = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(text_state, context, asset_store);
    if (!saved.ok()) return fail("Point and paragraph typography must save canonically");
    const auto restored = photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(saved.document, asset_store);
    if (!restored.ok() || restored.state->layers != text_state.layers)
      return fail("Canonical text must preserve mode, paragraph spacing and fitted origin");
    typography.insert("paragraphSpacing", 20.5);
    layer.insert("textTypography", typography);
    text_state.layers = {layer};
    const auto changed = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(text_state, context, asset_store);
    if (!changed.ok() || changed.document.cache_key == saved.document.cache_key)
      return fail("Paragraph spacing must change canonical content/cache identity");
  }
  {
    auto posterize_state = asset_backed_state;
    auto layer = posterize_state.layers[0].toMap();
    photo_editor::pipeline::PhotoEffectInstance posterize;
    posterize.id = "canonical-posterize";
    posterize.type_id = "posterize_linear_rgb";
    posterize.posterize_levels = 7;
    const auto stack = photo_editor::desktop::encode_photo_layer_effects(std::vector{posterize});
    if (!stack) return fail("Posterize must encode for canonical persistence");
    layer.insert("effectStack", *stack);
    posterize_state.layers[0] = layer;
    const auto saved = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(
        posterize_state, context, asset_store);
    if (!saved.ok()) return fail("Canonical posterize save must be admitted");
    const auto restored = photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(
        saved.document, asset_store);
    if (!restored.ok() || restored.state->layers != posterize_state.layers)
      return fail("Canonical posterize settings must roundtrip exactly");
    posterize.posterize_levels = 9;
    const auto changed_stack = photo_editor::desktop::encode_photo_layer_effects(std::vector{posterize});
    if (!changed_stack) return 1;
    layer.insert("effectStack", *changed_stack);
    posterize_state.layers[0] = layer;
    const auto changed = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(
        posterize_state, context, asset_store);
    if (!changed.ok() || changed.document.cache_key == saved.document.cache_key ||
        changed.document.recipe_payload == saved.document.recipe_payload)
      return fail("Posterize levels must change canonical recipe and cache identity");
  }
  for (const auto* type : {"motion_blur", "unsharp_mask"}) {
    auto effect_state = asset_backed_state;
    auto layer = effect_state.layers[0].toMap();
    photo_editor::pipeline::PhotoEffectInstance effect;
    effect.id = "canonical-spatial";
    effect.type_id = type;
    effect.motion = {37.25, -48.5, photo_editor::pipeline::PhotoEffectEdgeMode::Reflect};
    effect.unsharp = {2.75, 1.85, 0.025, photo_editor::pipeline::PhotoEffectEdgeMode::Extend};
    const auto stack = photo_editor::desktop::encode_photo_layer_effects(std::vector{effect});
    if (!stack) return fail("Spatial effect must encode");
    layer.insert("effectStack", *stack);
    effect_state.layers[0] = layer;
    const auto saved = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(
        effect_state, context, asset_store);
    if (!saved.ok()) return fail("Spatial effect canonical admission");
    const auto restored = photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(
        saved.document, asset_store);
    if (!restored.ok() || restored.state->layers != effect_state.layers)
      return fail("Spatial settings must roundtrip through canonical asset storage");
    effect.motion.angle += 1;
    effect.unsharp.threshold += 0.01;
    const auto changed_stack = photo_editor::desktop::encode_photo_layer_effects(std::vector{effect});
    if (!changed_stack) return 1;
    layer.insert("effectStack", *changed_stack);
    effect_state.layers[0] = layer;
    const auto changed = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(
        effect_state, context, asset_store);
    if (!changed.ok() || changed.document.cache_key == saved.document.cache_key)
      return fail("Spatial parameters must change canonical cache identity");
  }
  const auto document_projected =
      photo_editor::desktop::
          project_canonical_v13_document_to_desktop_state_with_assets(
              asset_written.document,
              asset_store);
  if (!document_projected.ok() ||
      document_projected.state->active_history_branch != asset_backed_state.active_history_branch ||
      document_projected.state->recipe_snapshots != asset_backed_state.recipe_snapshots ||
      document_projected.state->output_color_space != asset_backed_state.output_color_space ||
      document_projected.state->layers !=
          asset_backed_state.layers ||
      document_projected.state->preset_applications !=
          asset_backed_state.preset_applications) {
    return fail("stored canonical documents must preserve layers, presets and history with verified cache identity");
  }
  {
    auto inconsistent = *asset_recipe;
    const auto changed = std::find_if(inconsistent.layers.begin(), inconsistent.layers.end(),
        [](const auto& layer) { return layer.id.value == "pixel-layer-1"; });
    if (changed == inconsistent.layers.end() || changed->effects.size() != 1)
      return fail("canonical persistence lost the photo effect stack");
    changed->effects[0].gaussian.sigma += 1;
    const auto mismatch = photo_editor::desktop::project_canonical_v13_to_desktop_state_with_assets(inconsistent, asset_store);
    if (mismatch.ok() || std::none_of(mismatch.errors.begin(), mismatch.errors.end(), [](const auto& error) {
          return error.path.ends_with(".effects");
        })) return fail("stored presentation cannot silently override canonical photo effects");
  }
  auto history_only_state = asset_backed_state;
  for (const auto& space : {"sRGB", "Display P3", "Adobe RGB", "ProPhoto RGB", "Rec.709", "Rec.2020"}) {
    auto delivery_state = state;
    delivery_state.output_color_space = QString::fromLatin1(space);
    const auto delivered = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13(delivery_state, context);
    if (!delivered.ok()) return fail("every supported delivery space must serialize");
    const auto restored = photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(
        delivered.document, asset_store);
    if (!restored.ok() || restored.state->output_color_space != delivery_state.output_color_space ||
        (delivery_state.output_color_space != "sRGB" && delivered.document.cache_key == written.document.cache_key))
      return fail("delivery color space must survive projection and participate in cache identity");
  }
  history_only_state = photo_editor::desktop::append_named_recipe_snapshot(
      history_only_state, QStringLiteral("Second look"), QStringLiteral("alternative"));
  const auto history_only = photo_editor::desktop::serialize_desktop_recipe_as_canonical_v13_with_assets(
      history_only_state, context, asset_store);
  if (!history_only.ok() || history_only.document.recipe_payload != asset_written.document.recipe_payload ||
      history_only.document.cache_key != asset_written.document.cache_key ||
      history_only.document.provenance_payload == asset_written.document.provenance_payload) {
    return fail("history must persist without changing active render or cache identity");
  }
  auto legacy_document = asset_written.document;
  auto legacy_metadata = QJsonDocument::fromJson(legacy_document.provenance_payload.toUtf8()).object();
  legacy_metadata.insert("schema", "photo-editor-edit-recipe-provenance:v1");
  legacy_metadata.remove("history");
  legacy_document.provenance_payload = QString::fromUtf8(QJsonDocument(legacy_metadata).toJson(QJsonDocument::Compact));
  const auto legacy_projection = photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(
      legacy_document, asset_store);
  if (!legacy_projection.ok() || legacy_projection.state->active_history_branch != "main" ||
      !legacy_projection.state->recipe_snapshots.isEmpty() ||
      legacy_projection.state->preset_applications != asset_backed_state.preset_applications) {
    return fail("v1 provenance must remain readable without inventing history");
  }
  for (const auto& bad_history : {QJsonValue(QJsonValue::Null), QJsonValue(QJsonObject{}),
       QJsonValue(QJsonObject{{"activeBranchId", ""}, {"snapshots", QJsonArray{}}}),
       QJsonValue(QJsonObject{{"activeBranchId", "main"}, {"snapshots", QJsonObject{}}})}) {
    auto invalid_document = asset_written.document;
    auto metadata = QJsonDocument::fromJson(invalid_document.provenance_payload.toUtf8()).object();
    metadata.insert("history", bad_history);
    invalid_document.provenance_payload = QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    if (photo_editor::desktop::project_canonical_v13_document_to_desktop_state_with_assets(
            invalid_document, asset_store).ok()) return fail("malformed v2 history must fail closed");
  }
  auto malformed_provenance_document =
      asset_written.document;
  malformed_provenance_document.provenance_payload =
      QStringLiteral("{\"schema\":\"unknown\",\"presetApplications\":[]}");
  if (photo_editor::desktop::
          project_canonical_v13_document_to_desktop_state_with_assets(
              malformed_provenance_document,
              asset_store)
          .ok()) {
    return fail("canonical document projection must reject malformed audit provenance");
  }
  auto tampered_document =
      asset_written.document;
  tampered_document.cache_key =
      QStringLiteral("tampered-cache-key");
  if (photo_editor::desktop::
          project_canonical_v13_document_to_desktop_state_with_assets(
              tampered_document,
              asset_store)
          .ok()) {
    return fail("canonical document projection must reject a mismatched cache key");
  }
  const auto prepared =
      photo_editor::desktop::
          prepare_desktop_recipe_persistence(
              QStringLiteral("asset:42"),
              asset_backed_state,
              photo_editor::desktop::
                  DecoderProvenanceResult{
                      .provenance =
                          QStringLiteral(
                              "qt-raster-linearization-v1|suffix:ppm"),
                      .raw_source = false,
                  },
              asset_root.path());
  if (!prepared.ok() ||
      prepared.legacy_recipe_json.isEmpty() ||
      prepared.canonical_document.recipe_payload.isEmpty()) {
    return fail("desktop persistence preparation must publish a compatibility pair");
  }
  const QJsonObject legacy_catalog_recipe{
      {QStringLiteral("recipeVersion"), 12},
      {QStringLiteral("exposure"), -4.0},
  };
  const QJsonObject canonical_catalog_recipe{
      {QStringLiteral("recipe_payload"),
       asset_written.document.recipe_payload},
      {QStringLiteral("cache_key"),
       asset_written.document.cache_key},
      {QStringLiteral("provenance_payload"),
       asset_written.document.provenance_payload},
  };
  const auto catalog_projection =
      photo_editor::desktop::
          project_catalog_adjustments_for_desktop(
              legacy_catalog_recipe,
              canonical_catalog_recipe,
              asset_root.path());
  const auto catalog_adjustments =
      photo_editor::desktop::parse_adjustment_patch_json(
          catalog_projection.adjustment_patch_json);
  if (!catalog_projection.ok() ||
      !catalog_projection.canonical_present ||
      !catalog_adjustments.ok() ||
      catalog_adjustments.state->exposure != 1.25 ||
      catalog_adjustments.state->active_history_branch != asset_backed_state.active_history_branch ||
      catalog_adjustments.state->recipe_snapshots != asset_backed_state.recipe_snapshots) {
    return fail("valid canonical catalog authority must replace a conflicting v12 projection");
  }
  auto invalid_catalog_recipe =
      canonical_catalog_recipe;
  invalid_catalog_recipe.insert(
      QStringLiteral("cache_key"),
      QStringLiteral("tampered"));
  const auto rejected_catalog_projection =
      photo_editor::desktop::
          project_catalog_adjustments_for_desktop(
              legacy_catalog_recipe,
              invalid_catalog_recipe,
              asset_root.path());
  if (rejected_catalog_projection.ok() ||
      !rejected_catalog_projection.adjustment_patch_json.isEmpty()) {
    return fail("invalid canonical catalog authority must fail closed without v12 fallback");
  }
  const auto sidecar_path =
      asset_root.filePath(
          QStringLiteral(
              "frame.raw.photo-editor-recipe.json"));
  const auto sidecar_written =
      photo_editor::desktop::
          write_canonical_v13_recipe_sidecar(
              sidecar_path,
              asset_backed_state,
              context);
  if (!sidecar_written.ok()) {
    return fail("canonical v13 sidecar must write an atomic envelope and adjacent assets");
  }
  const auto sidecar_read =
      photo_editor::desktop::
          read_canonical_v13_recipe_sidecar(
              sidecar_path);
  if (!sidecar_read.ok() ||
      !sidecar_read.state.has_value() ||
      sidecar_read.state->mask_stack !=
          photo_editor::desktop::normalize_mask_stack(
              asset_backed_state.mask_stack) ||
      sidecar_read.state->layers !=
          asset_backed_state.layers ||
      sidecar_read.state->active_history_branch != asset_backed_state.active_history_branch ||
      sidecar_read.state->recipe_snapshots != asset_backed_state.recipe_snapshots ||
      sidecar_read.state->preset_applications !=
          asset_backed_state.preset_applications) {
    return fail("canonical v13 sidecar must reopen through verified immutable assets");
  }
  auto divergent_asset_recipe = *asset_recipe;
  divergent_asset_recipe.masks.front()
      .sources.front()
      .geometry.position = 0.123456;
  const auto rejected_divergence =
      photo_editor::desktop::
          project_canonical_v13_to_desktop_state_with_assets(
              divergent_asset_recipe,
              asset_store);
  if (rejected_divergence.ok() ||
      rejected_divergence.state.has_value()) {
    return fail("asset payloads that diverge from canonical v13 structure must fail projection");
  }

  auto missing_identity = context;
  missing_identity.recipe_id.clear();
  const auto rejected_identity =
      photo_editor::desktop::
          serialize_desktop_recipe_as_canonical_v13(
              state,
              missing_identity);
  if (rejected_identity.ok() ||
      !rejected_identity.document.recipe_payload.isEmpty()) {
    return fail("canonical persistence must fail closed without recipe identity");
  }

  state.raw_boost = 12.0;
  const auto rejected_legacy_intent =
      photo_editor::desktop::
          serialize_desktop_recipe_as_canonical_v13(
              state,
              context);
  if (rejected_legacy_intent.ok() ||
      !rejected_legacy_intent.document.recipe_payload.isEmpty()) {
    return fail("canonical persistence must not silently discard unsupported legacy intent");
  }

  return EXIT_SUCCESS;
}
