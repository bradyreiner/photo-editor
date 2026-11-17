#include "catalog_asset_model.h"
#include "edit_recipe_state.h"
#include "photo_paint_surface.h"
#include "photo_render_utils.h"
#include "working_image.h"
#include <QImageReader>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <functional>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace photo_editor::desktop {
struct CatalogThumbnailReuseTestAccess {
  static void observeStarts(CatalogAssetModel& model, int* count) {
    for (const auto& watcher : model.thumbnail_watchers_)
      QObject::connect(watcher.get(), &QFutureWatcher<ThumbnailTaskResult>::started, &model, [count] { ++*count; });
  }
  static void snapshot(CatalogAssetModel& model, const QString& path) {
    model.timeline_asset_loading_suppressed_for_testing_ = true;
    model.snapshot_fetch_generation_ = ++model.snapshot_request_generation_;
    model.snapshot_fetch_active_ = true;
    model.setLoading(true);
    model.snapshot_process_.start(QStringLiteral("/bin/cat"), {path});
  }
  static bool idle(const CatalogAssetModel& model) {
    return !model.snapshot_fetch_active_ && !model.snapshot_parse_active_ &&
        model.active_thumbnail_asset_ids_.isEmpty() && model.pending_thumbnail_asset_ids_.isEmpty();
  }
  static QVector<CatalogAssetItem> items(const CatalogAssetModel& model) { return model.all_assets_; }
  static void relink(CatalogAssetModel& model, const QString& id, const QString& path) {
    model.relinked_file_paths_.insert(id, path);
  }
  static void addVirtualCopy(CatalogAssetModel& model) {
    auto copy = model.all_assets_.first();
    copy.asset_id = "virtual-copy";
    copy.virtual_copy = true;
    copy.source_asset_id = model.all_assets_.first().asset_id;
    copy.variant_name = "Copy";
    copy.variant_index = 1;
    model.virtual_copies_.push_back(copy);
  }
  static void forceInvalidation(CatalogAssetModel& model) {
    for (auto& item : model.all_assets_) item.thumbnail_provenance_token.clear();
  }
  static bool apply(CatalogAssetModel& model, const CatalogAssetItem& old) {
    ThumbnailTaskResult result;
    result.asset_id = old.asset_id;
    result.file_path = old.file_path;
    result.adjustment_patch_json = old.adjustment_patch_json;
    result.thumbnail_source = old.thumbnail_source;
    result.thumbnail_dependency_revisions = old.thumbnail_dependency_revisions;
    result.thumbnail_provenance_token = old.thumbnail_provenance_token;
    return model.applyThumbnailResult(result);
  }
};
}
using namespace photo_editor::desktop;
namespace {
int fail(const char* text) { std::cerr << text << '\n'; return 1; }
bool wait(const std::function<bool()>& ready) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}
bool photo(const QString& path, const QColor& color) {
  QImage image(180, 120, QImage::Format_RGB32);
  image.fill(color);
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && image.save(&file, "BMP") && file.commit();
}
QString thumbnail(const CatalogAssetModel& model, const QString& id) {
  return model.assetById(id).value("thumbnailSource").toString();
}

bool verify_composite_alpha(CatalogAssetModel& model, const QString& root) {
  const auto path = QDir(root).filePath("transparent-wide-gamut.png");
  QImage source(1600, 1600, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  for (int y = 0; y < source.height(); ++y)
    for (int x = 0; x < source.width(); ++x)
      source.setPixelColor(x, y, QColor(160, 55, 95, y < 800 ? 170 : 0));
  if (!source.save(path)) return false;
  QVariantMap paint{{"id", "hdr-paint"}, {"kind", "pixel"}, {"enabled", true},
      {"pixelWidth", 1600}, {"pixelHeight", 1600}, {"canvasWidth", 1600}, {"canvasHeight", 1600},
      {"opacity", 0.82}, {"scale", 1.0},
      {"paintTiles", empty_photo_paint_tiles(working_color_space("Linear Display P3"))}};
  PhotoPaintSurface surface;
  if (!surface.load(paint)) return false;
  for (int ty = 0; ty * kPhotoPaintTileEdge < 1600; ++ty)
    for (int tx = 0; tx * kPhotoPaintTileEdge < 1600; ++tx) {
      auto tile = surface.tile(tx, ty);
      for (int y = 0; y < tile.height(); ++y) {
        auto* row = reinterpret_cast<QRgbaFloat32*>(tile.scanLine(y));
        for (int x = 0; x < tile.width(); ++x) {
          const int gy = ty * kPhotoPaintTileEdge + y;
          // A broad, partially erased stripe survives both decode scales.
          const float alpha = gy >= 800 && gy < 1200 ? 0.04F : 0.35F;
          row[x] = QRgbaFloat32{1.8F * alpha, -0.02F * alpha, 0.3F * alpha, alpha};
        }
      }
      surface.store(tx, ty, tile);
    }
  paint.insert("paintTiles", surface.snapshot());
  EditRecipeState edited;
  edited.exposure = 0.7;
  edited.saturation = 12;
  edited.crop_rect_x = 0.125;
  edited.crop_rect_y = 0.125;
  edited.crop_rect_width = 0.75;
  edited.crop_rect_height = 0.75;
  edited.layers = {paint};
  const auto compare = [&](const QString& source_path, const EditRecipeState& state, const char* label) {
    auto items = CatalogThumbnailReuseTestAccess::items(model);
    const auto id = items[0].asset_id;
    const auto prior_url = thumbnail(model, id);
    items[0].file_path = source_path;
    items[0].online = true;
    items[0].adjustment_patch_json = serialize_edit_recipe_json(state);
    model.replaceAssetsForTesting(items);
    model.setVisibleAssetRange(0, model.rowCount() - 1);
    if (!wait([&] { return CatalogThumbnailReuseTestAccess::idle(model) &&
            !thumbnail(model, id).isEmpty() && thumbnail(model, id) != prior_url; })) {
      std::cerr << label << " publication failed id=" << id.toStdString()
                << " source=" << source_path.toStdString() << " url=" << thumbnail(model, id).toStdString()
                << " idle=" << CatalogThumbnailReuseTestAccess::idle(model) << '\n';
      return false;
    }
    const auto cache_path = QUrl(thumbnail(model, id)).toLocalFile();
    QImageReader reader(cache_path);
    const auto encoded_format = reader.format().toLower();
    const auto actual = reader.read();
    if (encoded_format != "png" || actual.isNull() || !actual.hasAlphaChannel() ||
        actual.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
      std::cerr << label << " invalid cache path=" << cache_path.toStdString()
                << " format=" << encoded_format.constData() << " size=" << actual.width() << 'x'
                << actual.height() << " alpha=" << actual.hasAlphaChannel()
                << " srgb=" << (actual.colorSpace() == QColorSpace(QColorSpace::SRgb))
                << " error=" << reader.errorString().toStdString() << '\n';
      return false;
    }
    auto reference = render_flattened_output_file("catalog-alpha-reference", source_path, state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32);
    if (reference.isNull()) {
      std::cerr << label << " full flattened reference failed\n";
      return false;
    }
    reference = reference.scaled(768, 768, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (reference.size() != actual.size()) {
      std::cerr << label << " size actual=" << actual.width() << 'x' << actual.height()
                << " expected=" << reference.width() << 'x' << reference.height() << '\n';
      return false;
    }
    int maximum_visible = 0, maximum_alpha = 0, maximum_interior = 0, partial = 0, interior = 0;
    QPoint worst;
    for (int y = 0; y < actual.height(); ++y) for (int x = 0; x < actual.width(); ++x) {
      const auto left = actual.pixelColor(x, y), right = reference.pixelColor(x, y);
      int visible = 0;
      for (int channel = 0; channel < 3; ++channel) {
        const int a = channel == 0 ? left.red() : channel == 1 ? left.green() : left.blue();
        const int b = channel == 0 ? right.red() : channel == 1 ? right.green() : right.blue();
        const int black_a = qRound(a * left.alphaF()), black_b = qRound(b * right.alphaF());
        const int white_a = black_a + 255 - left.alpha(), white_b = black_b + 255 - right.alpha();
        visible = std::max(visible, std::max(std::abs(black_a - black_b), std::abs(white_a - white_b)));
      }
      if (visible > maximum_visible) { maximum_visible = visible; worst = QPoint(x, y); }
      maximum_alpha = std::max(maximum_alpha, std::abs(left.alpha() - right.alpha()));
      partial += left.alpha() > 0 && left.alpha() < 255;
      // Hidden RGB at alpha zero is undefined. Retain a separate straight-RGB
      // check in visible, uniform-alpha interior patches.
      if (left.alpha() >= 128 && right.alpha() >= 128 && x > 1 && y > 1 &&
          x + 2 < actual.width() && y + 2 < actual.height() &&
          actual.pixelColor(x - 2, y).alpha() == left.alpha() &&
          actual.pixelColor(x + 2, y).alpha() == left.alpha() &&
          actual.pixelColor(x, y - 2).alpha() == left.alpha() &&
          actual.pixelColor(x, y + 2).alpha() == left.alpha()) {
        ++interior;
        maximum_interior = std::max(maximum_interior, std::max({std::abs(left.red() - right.red()),
            std::abs(left.green() - right.green()), std::abs(left.blue() - right.blue())}));
      }
    }
    std::cout << label << " max black/white composite RGB8=" << maximum_visible
              << " alpha8=" << maximum_alpha << " interior straight RGB8=" << maximum_interior
              << " partial=" << partial << " interior=" << interior
              << " worst=" << worst.x() << ',' << worst.y() << '\n';
    return maximum_visible <= 2 && maximum_alpha <= 1 && maximum_interior <= 2 &&
        partial > 0 && interior > 0;
  };
  if (!compare(path, edited, "Catalog cross-resolution graded paint/erase")) return false;
  const auto prior_pixels = QImage(QUrl(thumbnail(model,
      CatalogThumbnailReuseTestAccess::items(model)[0].asset_id)).toLocalFile());
  edited.exposure = -0.7;
  if (!compare(path, edited, "Catalog changed grade")) return false;
  const auto changed_pixels = QImage(QUrl(thumbnail(model,
      CatalogThumbnailReuseTestAccess::items(model)[0].asset_id)).toLocalFile());
  if (changed_pixels == prior_pixels) return false;

  // At the bounded decode resolution the oracle is the actual full flattened
  // output, so every transformed edge can be checked without conflating two
  // different interpolation footprints at different physical resolutions.
  const auto bounded_path = QDir(root).filePath("bounded-transform-source.png");
  if (!source.scaled(1024, 1024, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).save(bounded_path)) return false;
  edited.transform_rotation = 11.0;
  edited.transform_scale = 0.91;
  edited.transform_skew_x = 0.08;
  edited.transform_skew_y = -0.04;
  edited.perspective_vertical = 5.0;
  edited.perspective_horizontal = -3.0;
  return compare(bounded_path, edited, "Catalog crop before transformed geometry");
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return fail("Temporary directory unavailable");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/thumbnail_fixture");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root.path());
  app.setOrganizationName("LooksyThumbnailReuseTest");
  app.setApplicationName("IsolatedThumbnailReuse");
  QJsonArray assets;
  for (int i = 0; i < 3; ++i) {
    const auto path = root.filePath(QStringLiteral("photo-%1.bmp").arg(i));
    if (!photo(path, QColor(140 + i * 20, 40, 30))) return fail("Cannot write image fixture");
    assets.append(QJsonObject{{"asset_id", QString::number(i)},
        {"location", QJsonObject{{"root_path", root.path()}, {"relative_path", QFileInfo(path).fileName()}, {"online", true}}}});
  }
  const auto snapshot_path = root.filePath("snapshot.json");
  QFile snapshot_file(snapshot_path);
  if (!snapshot_file.open(QIODevice::WriteOnly)) return fail("Cannot write snapshot");
  snapshot_file.write(QJsonDocument(QJsonObject{{"assets", assets}}).toJson());
  snapshot_file.close();
  int publications = 0;
  int starts = 0;
  int expected_rows = 3;
  CatalogAssetModel model;
  CatalogThumbnailReuseTestAccess::observeStarts(model, &starts);
  QObject::connect(&model, &QAbstractItemModel::dataChanged,
      [&](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
        if (roles.contains(CatalogAssetModel::ThumbnailSourceRole)) ++publications;
      });
  const auto idle = [&] { return CatalogThumbnailReuseTestAccess::idle(model); };
  const auto all_ready = [&] {
    return idle() && model.rowCount() == expected_rows && !thumbnail(model, "0").isEmpty() &&
        !thumbnail(model, "1").isEmpty() && !thumbnail(model, "2").isEmpty();
  };
  CatalogThumbnailReuseTestAccess::snapshot(model, snapshot_path);
  if (!wait([&] { return model.rowCount() == 3; })) return fail("Snapshot worker did not publish rows");
  model.setVisibleAssetRange(0, 2);
  if (!wait(all_ready)) return fail("Initial real thumbnails did not finish");
  const auto initial = CatalogThumbnailReuseTestAccess::items(model);
  for (const auto& item : initial)
    if (item.thumbnail_provenance_token.isEmpty() || item.thumbnail_dependency_revisions.size() < 2)
      return fail("Real worker did not publish source and cached-file provenance");
  const auto initial_publications = publications;
  const auto initial_starts = starts;
  for (int refresh = 0; refresh < 10; ++refresh) {
    CatalogThumbnailReuseTestAccess::snapshot(model, snapshot_path);
    if (!wait(all_ready)) return fail("Repeated snapshot failed");
    for (const auto& item : initial)
      if (thumbnail(model, item.asset_id) != item.thumbnail_source) return fail("Unchanged snapshot replaced thumbnail URL");
  }
  if (publications != initial_publications || starts != initial_starts)
    return fail("Unchanged snapshots started workers or republished thumbnails");
  // Control: remove provenance to reproduce mandatory regeneration on refresh.
  CatalogThumbnailReuseTestAccess::forceInvalidation(model);
  CatalogThumbnailReuseTestAccess::snapshot(model, snapshot_path);
  if (!wait(all_ready) || publications != initial_publications + 3 || starts != initial_starts + 3)
    return fail("Forced-invalidation control did not republish three thumbnails");

  auto items = CatalogThumbnailReuseTestAccess::items(model);
  EditRecipeState edited;
  edited.exposure = 0.5;
  items[1].adjustment_patch_json = serialize_edit_recipe_json(edited);
  auto before = publications;
  const auto unchanged = thumbnail(model, items[0].asset_id);
  model.replaceAssetsForTesting(items);
  if (thumbnail(model, items[0].asset_id) != unchanged || !thumbnail(model, items[1].asset_id).isEmpty())
    return fail("Recipe change did not isolate invalidation");
  if (!wait(all_ready) || publications != before + 1) return fail("Recipe change should publish exactly one thumbnail");

  items = CatalogThumbnailReuseTestAccess::items(model);
  const auto old = items[0];
  const auto old_time = QFileInfo(old.file_path).lastModified();
  const auto old_size = QFileInfo(old.file_path).size();
  if (!photo(old.file_path, QColor(30, 40, 200))) return fail("Cannot atomically replace source");
  QFile replaced(old.file_path);
  if (!replaced.open(QIODevice::ReadWrite) || !replaced.setFileTime(old_time, QFileDevice::FileModificationTime))
    return fail("Cannot restore fixture mtime");
  replaced.close();
  if (QFileInfo(old.file_path).size() != old_size) return fail("Same-size source fixture changed size");
  if (CatalogThumbnailReuseTestAccess::apply(model, old)) return fail("Stale in-flight source revision was accepted");
  before = publications;
  model.replaceAssetsForTesting(items);
  if (!wait(all_ready) || publications != before + 1 || thumbnail(model, old.asset_id) == old.thumbnail_source)
    return fail("Same-size replacement with restored mtime reused stale thumbnail");
  QImage old_pixels(QUrl(old.thumbnail_source).toLocalFile());
  QImage new_pixels(QUrl(thumbnail(model, old.asset_id)).toLocalFile());
  if (old_pixels.isNull() || new_pixels.isNull() || old_pixels == new_pixels)
    return fail("Source replacement did not produce different pixels");

  items = CatalogThumbnailReuseTestAccess::items(model);
  const auto cache_path = QUrl(items[2].thumbnail_source).toLocalFile();
  if (!QFile::remove(cache_path)) return fail("Cannot remove cached thumbnail fixture");
  before = publications;
  model.replaceAssetsForTesting(items);
  if (!wait(all_ready) || publications != before + 1 || !QFileInfo::exists(cache_path))
    return fail("Missing cached file was not regenerated in isolation");

  const auto linked_path = root.filePath("linked.bmp");
  if (!photo(linked_path, QColor(20, 180, 50))) return fail("Cannot write linked image");
  items = CatalogThumbnailReuseTestAccess::items(model);
  EditRecipeState layered;
  layered.layers = {QVariantMap{{"id", "placed"}, {"kind", "source"}, {"sourcePath", QDir::current().relativeFilePath(linked_path)},
      {"enabled", true}, {"opacity", 1.0}, {"canvasWidth", 180}, {"canvasHeight", 120},
      {"pixelWidth", 180}, {"pixelHeight", 120}, {"x", 0}, {"y", 0}, {"scale", 1.0}}};
  items[2].adjustment_patch_json = serialize_edit_recipe_json(layered);
  model.replaceAssetsForTesting(items);
  if (!wait(all_ready)) return fail("Linked-image thumbnail did not render");
  items = CatalogThumbnailReuseTestAccess::items(model);
  if (!items[2].thumbnail_dependency_revisions.contains(linked_path)) return fail("Linked dependency missing from provenance");
  const auto linked_before = items[2].thumbnail_source;
  if (!photo(linked_path, QColor(190, 20, 80))) return fail("Cannot replace linked image");
  before = publications;
  model.replaceAssetsForTesting(items);
  if (!wait(all_ready) || publications != before + 1 || thumbnail(model, items[2].asset_id) == linked_before)
    return fail("Linked-image change did not isolate invalidation");
  const QImage old_linked_pixels(QUrl(linked_before).toLocalFile());
  const QImage new_linked_pixels(QUrl(thumbnail(model, items[2].asset_id)).toLocalFile());
  if (old_linked_pixels.isNull() || new_linked_pixels.isNull() || old_linked_pixels == new_linked_pixels)
    return fail("Linked-image replacement changed cache identity but retained stale pixels");

  items = CatalogThumbnailReuseTestAccess::items(model);
  const auto offline_preview = items[0].thumbnail_source;
  if (!QFile::rename(items[0].file_path, items[0].file_path + ".offline")) return fail("Cannot make source offline");
  items[0].online = false;
  items[0].thumbnail_source.clear();
  before = publications;
  model.replaceAssetsForTesting(items);
  if (!idle() || thumbnail(model, items[0].asset_id) != offline_preview || publications != before)
    return fail("Offline original lost its last preview or queued work");
  const auto offline_items = CatalogThumbnailReuseTestAccess::items(model);
  for (const auto& item : offline_items)
    if (item.asset_id == items[0].asset_id && item.search_preview_current) return fail("Offline fallback marked current");
  if (!QFile::rename(items[0].file_path + ".offline", items[0].file_path)) return fail("Cannot restore source");
  CatalogThumbnailReuseTestAccess::snapshot(model, snapshot_path);
  if (!wait(all_ready)) return fail("Online restoration did not render");
  CatalogThumbnailReuseTestAccess::addVirtualCopy(model);
  expected_rows = 4;
  model.replaceAssetsForTesting(CatalogThumbnailReuseTestAccess::items(model));
  if (!wait([&] { return all_ready() && !thumbnail(model, "virtual-copy").isEmpty(); }))
    return fail("Virtual-copy thumbnail did not render");
  const auto virtual_preview = thumbnail(model, "virtual-copy");
  before = starts;
  CatalogThumbnailReuseTestAccess::snapshot(model, snapshot_path);
  if (!wait(all_ready) || starts != before || thumbnail(model, "virtual-copy") != virtual_preview)
    return fail("Snapshot did not preserve virtual-copy thumbnail");
  const auto relink_path = root.filePath("relinked.bmp");
  if (!photo(relink_path, QColor(40, 80, 210))) return fail("Cannot create relink source");
  CatalogThumbnailReuseTestAccess::relink(model, "2", relink_path);
  before = publications;
  CatalogThumbnailReuseTestAccess::snapshot(model, snapshot_path);
  if (!wait(all_ready) || publications != before + 1 || model.assetById("2").value("filePath").toString() != relink_path)
    return fail("Relink did not isolate thumbnail invalidation");
  if (!verify_composite_alpha(model, root.path()))
    return fail("Catalog thumbnail must preserve edited composite color and paint/erase alpha");
  std::cout << "10 real snapshot refreshes: 0 workers/publications; forced control: 3; recipe/source/cache/linked/relink changes: 1 publication each; original and linked pixels changed; offline restoration, virtual copies, and stale-result rejection passed\n";
  return 0;
}
