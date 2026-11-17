#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "shoot_match.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace photo_editor::desktop;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool wait_for(const std::function<bool()>& predicate, int timeout = 60000) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(2);
  }
  return predicate();
}
QString patch(const EditRecipeState& state) {
  return serialize_adjustment_patch_json(state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
}
CatalogAssetItem asset(const QString& id, const QString& path, const EditRecipeState& state) {
  CatalogAssetItem result;
  result.asset_id = id; result.file_path = path; result.media_type = QStringLiteral("photo");
  result.online = true; result.recipe_loaded = true; result.adjustment_patch_json = patch(state);
  return result;
}
QString saved(const CatalogAssetModel& catalog, const QString& id) { return catalog.assetById(id).value("adjustmentPatchJson").toString(); }
void analyze(CatalogAssetModel& catalog, const QVariantMap& options = {}) {
  require(catalog.prepareShootMatch("reference", {"a", "b"}, options), "Admit immutable reference and target snapshots");
  require(wait_for([&] { return !catalog.shootMatchBusy(); }), "Matching finishes without blocking the UI loop");
  if (catalog.shootMatchState().value("state").toString() != QStringLiteral("review")) {
    std::cerr << QJsonDocument::fromVariant(catalog.shootMatchState()).toJson().constData();
    throw std::runtime_error("Matching produces a review");
  }
}
QQuickItem* find_item(QQuickItem* item, const QString& name) {
  if (item->objectName() == name) return item;
  for (auto* child : item->childItems()) if (auto* found = find_item(child, name)) return found;
  return nullptr;
}

void verify_dialog(CatalogAssetModel& catalog) {
  QQmlEngine engine;
  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& errors) {
    for (const auto& error : errors) warnings.append(error.toString());
  });
  QQuickWindow window; window.resize(900, 720); window.show();
  QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/ShootMatchDialog.qml")));
  std::unique_ptr<QObject> dialog(component.createWithInitialProperties({{"catalog", QVariant::fromValue(&catalog)},
      {"assetIds", QStringList{"reference", "a", "b"}}, {"referenceId", "reference"},
      {"parent", QVariant::fromValue(window.contentItem())}}));
  require(bool(dialog), qPrintable(component.errorString()));
  require(QMetaObject::invokeMethod(dialog.get(), "open"), "Open the actual matching popup");
  require(wait_for([&] { return dialog->property("opened").toBool(); }), "Popup opens in a real Qt Quick window");
  const auto proof = qEnvironmentVariable("PHOTO_EDITOR_SHOOT_MATCH_PROOF_DIR");
  if (!proof.isEmpty()) {
    require(QDir().mkpath(proof), "Create matching UI evidence");
    require(window.grabWindow().save(QDir(proof).filePath("matching-options.png")), "Capture matching options");
  }
  auto* build = find_item(window.contentItem(), QStringLiteral("shootMatchApplyButton"));
  require(build && build->isEnabled(), "Build previews is reachable");
  build->forceActiveFocus(); QTest::keyClick(&window, Qt::Key_Space);
  require(wait_for([&] { return dialog->property("page").toInt() == 1 && !catalog.shootMatchBusy(); }), "Keyboard activation builds actual review previews");
  auto* list = find_item(window.contentItem(), QStringLiteral("shootMatchReviewList"));
  require(list && list->isVisible(), "Review uses the production result list");
  require(wait_for([&] {
    for (const auto& name : {"shootMatchReferencePreview", "shootMatchPreview_a_0", "shootMatchPreview_a_1", "shootMatchPreview_b_0", "shootMatchPreview_b_1"}) {
      const auto* image = find_item(window.contentItem(), QString::fromLatin1(name));
      if (!image || image->property("status").toInt() != 1) return false;
    }
    return true;
  }, 15000), "Every reference, before and after image must decode before accepting UI proof");
  const auto result_rows = catalog.shootMatchState().value("results").toList();
  for (const auto& value : result_rows) {
    const auto row = value.toMap();
    if (row.value("outlier").toBool()) require(!dialog->property("checkedIds").toStringList().contains(row.value("assetId").toString()), "Outliers start unchecked");
  }
  for (const auto& size : {QSize(900, 720), QSize(600, 600)}) {
    window.resize(size); QTest::qWait(60); QCoreApplication::processEvents();
    auto* apply = find_item(window.contentItem(), QStringLiteral("shootMatchApplyButton"));
    auto* close = find_item(window.contentItem(), QStringLiteral("shootMatchCloseButton"));
    for (auto* control : {apply, close, list}) {
      require(control && control->isVisible(), "Review controls stay visible at compact size");
      const auto bounds = control->mapRectToScene(control->boundingRect());
      require(QRectF(QPointF(0, 0), QSizeF(size)).adjusted(-1, -1, 1, 1).contains(bounds), "Popup controls stay inside the window");
    }
    if (!proof.isEmpty()) require(window.grabWindow().save(QDir(proof).filePath(QStringLiteral("matching-review-%1.png").arg(size.width()))), "Capture production comparison previews");
  }
  require(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
  QMetaObject::invokeMethod(dialog.get(), "close");
}
}

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/shoot-match-test");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  try {
    require(AppBootstrap::ensureRuntimeDirectories(), "Create isolated runtime storage");
    QImage image(160, 120, QImage::Format_ARGB32); image.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
      const auto grey = 45 + x * 150 / image.width();
      image.setPixelColor(x, y, y < 70 ? QColor(grey, grey, grey) : QColor(grey, grey * 3 / 4, grey / 2));
    }
    const bool real_photo = application.arguments().contains("--photo");
    if (real_photo) require(image.load(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/fixtures/creative-tools/landscape-mountain-source.jpg")), "Read a real photographic source");
    const auto source = runtime.filePath("source.png");
    const auto second_source = runtime.filePath("second.png");
    require(image.save(source) && image.save(second_source), "Save matching source images");
    EditRecipeState reference;
    reference.photographic_rendering_model = real_photo ? QStringLiteral("aces2-ocio-v1") : QStringLiteral("legacy-oklch-v1");
    reference.crop_rect_x = .05; reference.crop_rect_y = .05; reference.crop_rect_width = .9; reference.crop_rect_height = .9;
    reference.layers = {QVariantMap{{"id", "retained-layer"}, {"kind", "fill"}, {"enabled", false}, {"name", "Keep this layer"}, {"fillColor", "#223344"}}};
    QByteArray mask_alpha(16 * 16, '\0');
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) mask_alpha[y * 16 + x] = char(x * 17);
    reference.mask_stack = {QVariantMap{{"id", "retained-soft-mask"}, {"kind", "background"}, {"enabled", true},
        {"operation", "add"}, {"exposure", .1}, {"temperature", 5.0}, {"opacity", .8}, {"density", 1.0},
        {"segmentationWidth", 16}, {"segmentationHeight", 16}, {"segmentationData", mask_alpha}}};
    reference.active_mask_index = 0;
    auto first = reference; first.exposure = -.85; first.temperature = -12; first.tint = 8; first.contrast = -12; first.saturation = -18;
    first.sharpening = 5; first.output_color_space = "Display P3";
    auto second = reference; second.exposure = .65; second.contrast = 10; second.saturation = 15;
    const QVector<CatalogAssetItem> assets{asset("reference", source, reference), asset("a", source, first), asset("b", second_source, second)};
    CatalogAssetModel catalog; catalog.replaceAssetsForTesting(assets);
    if (application.arguments().contains("--ui")) { verify_dialog(catalog); return 0; }
    const auto initial_a = saved(catalog, "a"), initial_b = saved(catalog, "b"), initial_reference = saved(catalog, "reference");
    const auto initial_history = catalog.batchRecipeHistoryCurrentIndex();
    require(!catalog.prepareShootMatch("reference", {"a"}, {{"lighting", false}, {"look", false}}), "Reject matching with no enabled adjustments");
    require(!catalog.prepareShootMatch("reference", {"missing"}, {}), "Reject an unknown target without mutation");
    analyze(catalog);
    require(saved(catalog, "a") == initial_a && saved(catalog, "b") == initial_b && catalog.batchRecipeHistoryCurrentIndex() == initial_history,
        "Analysis and previews never change catalog recipes or history");
    const auto rows = catalog.shootMatchState().value("results").toList();
    require(rows.size() == 2, "One proposal is produced per target");
    for (const auto& value : rows) {
      const auto row = value.toMap();
      require(row.value("ok").toBool() && row.value("changed").toBool(), "Known shifted targets produce editable improvements");
      require(row.value("afterDistance").toDouble() < row.value("beforeDistance").toDouble() * .8,
          "Matching measurably reduces the known reference discrepancy");
      require(!QImage(QUrl(row.value("afterSource").toString()).toLocalFile()).isNull(), "Every proposal has a real rendered preview");
      require(!row.contains("afterPatchJson"), "Large private proposal recipes do not enter the QML review model");
    }
    require(catalog.applyReviewedShootMatch({"a", "b"}), "Apply two independently fitted recipes in one batch");
    const auto applied_a = saved(catalog, "a"), applied_b = saved(catalog, "b");
    require(catalog.batchRecipeHistoryCurrentIndex() == initial_history + 1 && saved(catalog, "reference") == initial_reference,
        "One batch history entry owns all proposals and leaves the reference intact");
    auto changed = *parse_adjustment_patch_json(applied_a).state;
    const auto original = *parse_adjustment_patch_json(initial_a).state;
    changed.exposure = original.exposure; changed.temperature = original.temperature; changed.tint = original.tint;
    changed.contrast = original.contrast; changed.saturation = original.saturation;
    require(changed == original, "Matching preserves every field outside the five supported global adjustments");
    {
      const auto accepted = *parse_adjustment_patch_json(applied_a).state;
      QImage decoded;
      photo_editor::pipeline::DecodedImageMetadata metadata;
      require(decode_source_image(source, &decoded, 640, accepted, &metadata), "Decode the accepted source recipe");
      const auto exported = render_output_image_with_backend(decoded, accepted, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
          RenderBackendPreference::CpuOnly, &metadata);
      const auto proposal = rows.first().toMap();
      const QImage displayed(QUrl(proposal.value("afterSource").toString()).toLocalFile());
      require(exported.convertToFormat(QImage::Format_ARGB32) == displayed.convertToFormat(QImage::Format_ARGB32),
          "The accepted durable recipe reproduces the exact reviewed output through the canonical renderer");
    }
    catalog.undoBatchRecipe();
    require(saved(catalog, "a") == initial_a && saved(catalog, "b") == initial_b, "One Undo restores both exact original recipes");
    catalog.redoBatchRecipe();
    require(saved(catalog, "a") == applied_a && saved(catalog, "b") == applied_b, "Redo restores both accepted proposals exactly");
    { CatalogAssetModel reopened; reopened.replaceAssetsForTesting(assets);
      require(saved(reopened, "a") == applied_a && saved(reopened, "b") == applied_b, "Durable recipe overrides survive reopening the catalog model"); }
    require(catalog.applyAdjustmentPatchJsonToAssetIds({"a"}, initial_a) == 1, "Reset one target to a known exposure discrepancy");
    analyze(catalog, {{"lighting", true}, {"look", false}, {"lightingAmount", .5}});
    require(catalog.applyReviewedShootMatch({"a"}) && saved(catalog, "b") == applied_b,
        "Applying a chosen subset leaves unselected proposals untouched");
    const auto lighting_only = *parse_adjustment_patch_json(saved(catalog, "a")).state;
    require(lighting_only.contrast == original.contrast && lighting_only.saturation == original.saturation,
        "Disabling Look preserves contrast and saturation");
    const auto before_look = lighting_only;
    analyze(catalog, {{"lighting", false}, {"look", true}, {"lookAmount", .5}});
    bool has_look_proposal = false;
    for (const auto& value : catalog.shootMatchState().value("results").toList()) {
      const auto row = value.toMap();
      if (row.value("assetId").toString() == QStringLiteral("a")) has_look_proposal = row.value("changed").toBool();
      const auto changes = row.value("changes").toMap();
      require(changes.value("exposure").toDouble() == 0 && changes.value("temperature").toDouble() == 0 && changes.value("tint").toDouble() == 0,
          "Look-only proposals never alter lighting fields");
    }
    if (has_look_proposal) require(catalog.applyReviewedShootMatch({"a"}), "Apply the reviewed Look-only proposal");
    else require(!catalog.applyReviewedShootMatch({"a"}), "An unchanged target cannot create a misleading history entry");
    const auto after_look = *parse_adjustment_patch_json(saved(catalog, "a")).state;
    require(after_look.exposure == before_look.exposure && after_look.temperature == before_look.temperature && after_look.tint == before_look.tint,
        "Disabling Lighting preserves exposure and white balance");
    analyze(catalog);
    auto intervening = *parse_adjustment_patch_json(saved(catalog, "b")).state; intervening.exposure += .1;
    require(catalog.applyAdjustmentPatchJsonToAssetIds({"b"}, patch(intervening)) == 1, "Make an intervening target edit");
    const auto untouched_a = saved(catalog, "a"), edited_b = saved(catalog, "b");
    const auto history = catalog.batchRecipeHistoryCurrentIndex();
    require(!catalog.applyReviewedShootMatch({"a", "b"}) && saved(catalog, "a") == untouched_a && saved(catalog, "b") == edited_b &&
        catalog.batchRecipeHistoryCurrentIndex() == history, "A stale target rejects the whole selected commit before any mutation");
    require(catalog.prepareShootMatch("reference", {"a", "b"}, {}), "Start a cancellable analysis");
    catalog.cancelShootMatch();
    require(wait_for([&] { return !catalog.shootMatchBusy(); }) && catalog.shootMatchState().value("state").toString() == "cancelled" &&
        saved(catalog, "a") == untouched_a && saved(catalog, "b") == edited_b, "Cancellation preserves accepted edits");
    analyze(catalog);
    image.fill(Qt::green); require(image.save(second_source), "Replace a reviewed source file");
    require(!catalog.applyReviewedShootMatch({"a", "b"}) && saved(catalog, "a") == untouched_a, "Source replacement invalidates proposals before application");
    std::cout << "Shoot matching passed render improvement, field preservation, review, history, reopen, stale edit/source and cancellation checks\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
