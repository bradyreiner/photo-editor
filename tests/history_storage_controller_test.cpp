#include "editor_preview_controller.h"
#include "photo_layer_clipboard.h"
#include "photo_paint_surface.h"
#include <QCoreApplication>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <sys/resource.h>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <iostream>
#if defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/task_info.h>
#include <malloc/malloc.h>
#endif

namespace photo_editor::desktop {
constexpr qint64 kHistoryPayloadBudgetBytes = 32LL * 1024 * 1024;
struct EditorHistoryStorageTestAccess {
  static void setup(EditorPreviewController& controller, const QString& directory) {
    controller.history_service_.configureStorage(directory, 8 * 1024 * 1024);
    controller.asset_id_ = "history-storage-a";
  }
  static void append(EditorPreviewController& controller, const EditRecipeState& state, int index) {
    controller.applyState(state);
    controller.pushUndoState(state, QStringLiteral("Revision %1").arg(index));
  }
  static void stressSetup(EditorPreviewController& controller, const QString& directory, bool baseline) {
    if (baseline) controller.history_service_ = HistoryService{};
    else controller.history_service_.configureStorage(directory, kHistoryPayloadBudgetBytes);
    controller.asset_id_ = "history-storage-stress";
  }
  static void apply(EditorPreviewController& controller, const EditRecipeState& state) { controller.applyState(state); }
  static EditRecipeState current(const EditorPreviewController& controller) { return controller.currentState(); }
  static void wait(EditorPreviewController& controller) { controller.history_service_.waitForStorage(); }
  static QVariantMap stats(const EditorPreviewController& controller) { return controller.history_service_.storageStatistics(); }
  static int residentCount(const EditorPreviewController& controller) {
    int count = 0;
    for (const auto& revision : controller.history_service_.undoStackValue(controller.asset_id_))
      if (revision.residentState()) ++count;
    return count;
  }
  static void unpin(EditorPreviewController& controller) {
    for (const auto& revision : controller.history_service_.undoStackValue(controller.asset_id_)) revision.unpin();
    for (const auto& revision : controller.history_service_.redoStackValue(controller.asset_id_)) revision.unpin();
    wait(controller);
  }
  static void clear(EditorPreviewController& controller) {
    controller.history_service_.clearAssetHistory(controller.asset_id_);
  }
  static bool formatReady(const EditorPreviewController& controller) {
    return !controller.history_format_busy_ && !controller.history_format_ready_key_.isEmpty();
  }
  static bool preview(const EditorPreviewController& controller) { return controller.history_preview_state_.has_value(); }
  static quint64 generation(const EditorPreviewController& controller) { return controller.history_state_generation_; }
  static bool pending(const EditorPreviewController& controller) { return controller.history_requested_index_ >= 0; }
};
}
using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QByteArray bytes(int revision) { return QByteArray(1024 * 1024, static_cast<char>(revision)); }
EditRecipeState state(int revision) {
  EditRecipeState result;
  result.exposure = static_cast<double>(revision) / 100.0;
  result.recipe_snapshots = {QVariantMap{{"id", "nested-payload"}, {"binary", bytes(revision)}}};
  return result;
}
bool exact(const EditorPreviewController& controller, int revision) {
  const auto current = EditorHistoryStorageTestAccess::current(controller);
  return current.exposure == static_cast<double>(revision) / 100.0 &&
      current.recipe_snapshots == state(revision).recipe_snapshots;
}
bool until(const std::function<bool()>& done, int limit = 10000) {
  QElapsedTimer timer; timer.start();
  while (!done() && timer.elapsed() < limit) { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QThread::msleep(1); }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  return done();
}
void settle() { QElapsedTimer t; t.start(); until([&] { return t.elapsed() >= 150; }); }

struct ProcessMemorySample final {
  qint64 peak_rss_bytes = 0;
  qint64 resident_bytes = 0;
  qint64 physical_footprint_bytes = 0;
  qint64 all_zone_malloc_live_bytes = 0;
  qint64 all_zone_malloc_allocated_bytes = 0;
};

ProcessMemorySample process_memory_sample() {
  ProcessMemorySample sample;
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    sample.peak_rss_bytes = static_cast<qint64>(usage.ru_maxrss);
#if !defined(Q_OS_MACOS)
    sample.peak_rss_bytes *= 1024;
#endif
  }
#if defined(Q_OS_MACOS)
  mach_task_basic_info_data_t resident_info {};
  mach_msg_type_number_t resident_count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
          reinterpret_cast<task_info_t>(&resident_info), &resident_count) == KERN_SUCCESS)
    sample.resident_bytes = static_cast<qint64>(resident_info.resident_size);
  task_vm_info_data_t task_info_data {};
  mach_msg_type_number_t task_info_count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO,
          reinterpret_cast<task_info_t>(&task_info_data), &task_info_count) == KERN_SUCCESS) {
    sample.physical_footprint_bytes = static_cast<qint64>(task_info_data.phys_footprint);
  }
  malloc_statistics_t statistics {};
  // nullptr asks libmalloc to sum statistics across all zones.
  malloc_zone_statistics(nullptr, &statistics);
  sample.all_zone_malloc_live_bytes = static_cast<qint64>(statistics.size_in_use);
  sample.all_zone_malloc_allocated_bytes = static_cast<qint64>(statistics.size_allocated);
#endif
  return sample;
}

double process_cpu_ms() {
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0.0;
  return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0 +
      static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
}

QJsonObject memory_json(const ProcessMemorySample& sample) {
  return {{QStringLiteral("peak_rss_bytes"), sample.peak_rss_bytes},
      {QStringLiteral("resident_bytes"), sample.resident_bytes},
      {QStringLiteral("physical_footprint_bytes"), sample.physical_footprint_bytes},
      {QStringLiteral("all_zone_malloc_live_bytes"), sample.all_zone_malloc_live_bytes},
      {QStringLiteral("all_zone_malloc_allocated_bytes"), sample.all_zone_malloc_allocated_bytes}};
}

qint64 disk_bytes(const QString& directory) {
  if (directory.isEmpty()) return 0;
  qint64 total = 0;
  QDirIterator files(directory, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (files.hasNext()) total += QFileInfo(files.next()).size();
  return total;
}

double percentile_ms(QList<double> samples, double fraction) {
  if (samples.isEmpty()) return 0.0;
  std::sort(samples.begin(), samples.end());
  const auto index = static_cast<qsizetype>(std::ceil(fraction * static_cast<double>(samples.size() - 1)));
  return samples.at(index);
}

QJsonArray sample_array(const QList<double>& samples) {
  QJsonArray result;
  for (const auto sample : samples) result.append(sample);
  return result;
}

QByteArray sha256(const QByteArray& bytes) {
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QByteArray hash_utf16(const QString& value) {
  const auto bytes = QByteArray::fromRawData(
      reinterpret_cast<const char*>(value.constData()), value.size() * static_cast<qsizetype>(sizeof(QChar)));
  return sha256(bytes);
}

QByteArray hash_image(const QImage& image) {
  if (image.isNull()) return {};
  const auto bytes = QByteArray::fromRawData(
      reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
  return sha256(bytes);
}

QByteArray hash_recipe(const EditRecipeState& state) {
  const auto encoded = serialize_edit_recipe_json(state).toUtf8();
  return encoded.isEmpty() ? QByteArray{} : sha256(encoded);
}

QImage history_fixture_tile(quint32 seed, bool uniform = false) {
  QImage image(kPhotoPaintTileEdge, kPhotoPaintTileEdge,
      QImage::Format_RGBA32FPx4_Premultiplied);
  image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  auto* const pixels = reinterpret_cast<float*>(image.bits());
  auto next = seed + 1U;
  const auto random_unit = [&next]() {
    next = next * 1664525U + 1013904223U;
    return static_cast<float>((next >> 8U) & 0xffffU) / 65535.0f;
  };
  const auto uniform_red = static_cast<float>((seed * 97U + 1234U) & 0xffffU) / 65535.0f;
  const auto uniform_green = static_cast<float>((seed * 193U + 4321U) & 0xffffU) / 65535.0f;
  const auto uniform_blue = static_cast<float>((seed * 389U + 8765U) & 0xffffU) / 65535.0f;
  for (int pixel = 0; pixel < kPhotoPaintTileEdge * kPhotoPaintTileEdge; ++pixel) {
    pixels[pixel * 4] = uniform ? uniform_red : random_unit();
    pixels[pixel * 4 + 1] = uniform ? uniform_green : random_unit();
    pixels[pixel * 4 + 2] = uniform ? uniform_blue : random_unit();
    pixels[pixel * 4 + 3] = 1.0f;
  }
  return image;
}

constexpr int kHistoryPaintTileCount = 4;
constexpr int kHistoryPaintRevisionCount = 96;
constexpr int kHistoryPaintCanvasWidth = kHistoryPaintTileCount * kPhotoPaintTileEdge;
constexpr int kHistoryPaintCanvasHeight = kPhotoPaintTileEdge;

QVariantMap history_paint_layer(const QString& id, const QVariantMap& tiles) {
  return {{QStringLiteral("id"), id}, {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("name"), id}, {QStringLiteral("pixelWidth"), kHistoryPaintCanvasWidth},
      {QStringLiteral("pixelHeight"), kHistoryPaintCanvasHeight},
      {QStringLiteral("canvasWidth"), kHistoryPaintCanvasWidth},
      {QStringLiteral("canvasHeight"), kHistoryPaintCanvasHeight},
      {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("rotation"), 0.0},
      {QStringLiteral("scale"), 1.0}, {QStringLiteral("enabled"), true},
      {QStringLiteral("opacity"), 1.0}, {QStringLiteral("paintTiles"), tiles}};
}

QByteArray history_fixture_mask_bytes() {
  QByteArray data(1024 * 1024, Qt::Uninitialized);
  quint32 value = 0x81e10da5U;
  for (auto& byte : data) {
    value = value * 1664525U + 1013904223U;
    byte = static_cast<char>(value >> 24U);
  }
  return data;
}

QVariantMap history_fixture_mask(const QByteArray& bytes) {
  auto mask = default_mask_definition(QStringLiteral("brush"), 1);
  mask.insert(QStringLiteral("segmentationData"), bytes);
  mask.insert(QStringLiteral("segmentationWidth"), 1024);
  mask.insert(QStringLiteral("segmentationHeight"), 1024);
  return mask;
}

QVariantMap history_fixture_snapshot(int revision, const QString& repeated_unicode) {
  const auto label = QStringLiteral("Étude / 雪景 / 🌌 %1").arg(revision);
  return {{QStringLiteral("id"), QStringLiteral("history-snapshot-%1").arg(revision)},
      {QStringLiteral("name"), label}, {QStringLiteral("branchId"), QStringLiteral("main")},
      {QStringLiteral("adjustmentPatch"), QVariantMap{
          {QStringLiteral("format"), QStringLiteral("photo-editor-adjustment-patch")},
          {QStringLiteral("version"), 1}, {QStringLiteral("scope"), QStringLiteral("asset-bound")},
          {QStringLiteral("groups"), QVariantList{}},
          {QStringLiteral("adjustmentState"), QVariantMap{}}}},
      {QStringLiteral("label"), label}, {QStringLiteral("unicode"), repeated_unicode},
      {QStringLiteral("unicodeCopy"), repeated_unicode}};
}

EditRecipeState initial_paint_history_state(const QByteArray& mask_bytes,
                                            const QString& repeated_unicode,
                                            QVector<QByteArray>* pixel_hashes,
                                            bool uniform = false) {
  PhotoPaintSurface surface;
  surface.size = QSize(kHistoryPaintCanvasWidth, kHistoryPaintCanvasHeight);
  surface.color_space = QColorSpace(QColorSpace::SRgbLinear);
  surface.payload = empty_photo_paint_tiles(surface.color_space);
  pixel_hashes->clear();
  pixel_hashes->reserve(kHistoryPaintTileCount);
  for (int index = 0; index < kHistoryPaintTileCount; ++index) {
    const auto image = history_fixture_tile(static_cast<quint32>(index + 1), uniform);
    surface.store(index, 0, image);
    pixel_hashes->append(hash_image(image));
  }
  const auto tiles = surface.snapshot();
  EditRecipeState state;
  state.layers = {history_paint_layer(QStringLiteral("paint-a"), tiles),
      history_paint_layer(QStringLiteral("paint-b"), tiles)};
  state.mask_stack = {history_fixture_mask(mask_bytes)};
  state.active_mask_index = 0;
  state.recipe_snapshots = {history_fixture_snapshot(0, repeated_unicode)};
  return state;
}

struct PaintHistoryReceipt final {
  QByteArray recipe_digest;
  std::array<QByteArray, kHistoryPaintTileCount> compressed_tile_digests;
  std::array<qint64, kHistoryPaintTileCount> tile_payload_utf16_bytes{};
  std::array<QByteArray, kHistoryPaintTileCount> pixel_digests;
  qint64 unique_tile_payload_utf16_bytes = 0;
  QByteArray mask_digest;
  QString unicode_label;
};

std::optional<PaintHistoryReceipt> paint_history_receipt(
    const EditRecipeState& state, const QVector<QByteArray>& pixel_digests) {
  if (state.layers.size() != 2 || state.mask_stack.size() != 1 ||
      pixel_digests.size() != kHistoryPaintTileCount) return std::nullopt;
  PaintHistoryReceipt receipt;
  receipt.recipe_digest = hash_recipe(state);
  if (receipt.recipe_digest.isEmpty()) return std::nullopt;
  receipt.pixel_digests = {pixel_digests.at(0), pixel_digests.at(1),
      pixel_digests.at(2), pixel_digests.at(3)};
  const auto mask = state.mask_stack.first().toMap();
  receipt.mask_digest = sha256(mask.value(QStringLiteral("segmentationData")).toByteArray());
  const auto snapshot = state.recipe_snapshots.first().toMap();
  receipt.unicode_label = snapshot.value(QStringLiteral("label")).toString();
  QVariantMap first_tiles;
  for (int layer_index = 0; layer_index < state.layers.size(); ++layer_index) {
    const auto layer = state.layers.at(layer_index).toMap();
    const auto paint_tiles = layer.value(QStringLiteral("paintTiles")).toMap();
    const auto tiles = paint_tiles.value(QStringLiteral("tiles")).toMap();
    if (tiles.size() != kHistoryPaintTileCount) return std::nullopt;
    if (layer_index == 0) first_tiles = tiles;
    else if (tiles != first_tiles) return std::nullopt;
  }
  for (int index = 0; index < kHistoryPaintTileCount; ++index) {
    const auto key = PhotoPaintSurface::key(index, 0);
    const auto tile = first_tiles.value(key).toString();
    if (tile.isEmpty()) return std::nullopt;
    receipt.compressed_tile_digests[static_cast<std::size_t>(index)] = hash_utf16(tile);
    const auto bytes = static_cast<qint64>(tile.size()) * static_cast<qint64>(sizeof(QChar));
    receipt.tile_payload_utf16_bytes[static_cast<std::size_t>(index)] = bytes;
    receipt.unique_tile_payload_utf16_bytes += bytes;
  }
  return receipt;
}

bool exact_paint_history_receipt(const EditorPreviewController& controller,
                                 const PaintHistoryReceipt& receipt) {
  const auto state = EditorHistoryStorageTestAccess::current(controller);
  if (hash_recipe(state) != receipt.recipe_digest || state.layers.size() != 2 ||
      state.mask_stack.size() != 1 || state.mask_stack.first().toMap()
          .value(QStringLiteral("segmentationData")).toByteArray().isEmpty() ||
      sha256(state.mask_stack.first().toMap().value(QStringLiteral("segmentationData")).toByteArray()) != receipt.mask_digest ||
      state.recipe_snapshots.isEmpty() || state.recipe_snapshots.first().toMap()
          .value(QStringLiteral("label")).toString() != receipt.unicode_label) return false;
  PhotoPaintSurface surface;
  if (!surface.load(state.layers.first().toMap())) return false;
  PhotoPaintSurface duplicate_surface;
  if (!duplicate_surface.load(state.layers.last().toMap()) ||
      duplicate_surface.tiles != surface.tiles) return false;
  for (int index = 0; index < kHistoryPaintTileCount; ++index) {
    const auto key = PhotoPaintSurface::key(index, 0);
    if (hash_utf16(surface.tiles.value(key).toString()) !=
        receipt.compressed_tile_digests[static_cast<std::size_t>(index)]) return false;
    QString error;
    const auto pixels = surface.tile(index, 0, &error);
    if (!error.isEmpty() || hash_image(pixels) !=
        receipt.pixel_digests[static_cast<std::size_t>(index)]) return false;
  }
  return true;
}

QByteArray legacy_clipboard_bytes(const PhotoLayerClipboard& value) {
  const auto recipe_json = serialize_edit_recipe_json(value.state);
  if (recipe_json.isEmpty()) return {};
  const QJsonObject object{{QStringLiteral("schema"), QStringLiteral("looksy-photo-layers-v1")},
      {QStringLiteral("width"), value.canvas.width()}, {QStringLiteral("height"), value.canvas.height()},
      {QStringLiteral("selectedIds"), QJsonArray::fromStringList(value.selected_ids)},
      {QStringLiteral("recipe"), QJsonDocument::fromJson(recipe_json.toUtf8()).object()}};
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  return bytes.size() <= maximum_photo_layer_clipboard_bytes ? bytes : QByteArray{};
}

QByteArray direct_clipboard_candidate_bytes(const PhotoLayerClipboard& value) {
  const auto recipe = build_edit_recipe_object(value.state);
  if (recipe.contains(QStringLiteral("componentObjectTableError")) ||
      recipe.contains(QStringLiteral("textDimensionError"))) return {};
  const QJsonObject object{{QStringLiteral("schema"), QStringLiteral("looksy-photo-layers-v1")},
      {QStringLiteral("width"), value.canvas.width()}, {QStringLiteral("height"), value.canvas.height()},
      {QStringLiteral("selectedIds"), QJsonArray::fromStringList(value.selected_ids)},
      {QStringLiteral("recipe"), recipe}};
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  return bytes.size() <= maximum_photo_layer_clipboard_bytes ? bytes : QByteArray{};
}

bool write_result(const QJsonObject& result, const QStringList& arguments) {
  const auto output_index = arguments.indexOf(QStringLiteral("--output"));
  if (output_index >= 0 && output_index + 1 < arguments.size()) {
    QFile file(arguments.at(output_index + 1));
    const auto json = QJsonDocument(result).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) return false;
  }
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
  return true;
}

int clipboard_stress(bool over_limit, const QStringList& arguments) {
  EditRecipeState state;
  QStringList selected_ids{QStringLiteral("paint-a"), QStringLiteral("paint-b")};
  QSize canvas(kHistoryPaintCanvasWidth, kHistoryPaintCanvasHeight);
  if (over_limit) {
    auto mask = default_mask_definition(QStringLiteral("brush"), 1);
    constexpr int width = 8192;
    constexpr int height = 6400;
    mask.insert(QStringLiteral("segmentationWidth"), width);
    mask.insert(QStringLiteral("segmentationHeight"), height);
    mask.insert(QStringLiteral("segmentationData"), QByteArray(width * height, '\x5a'));
    state.mask_stack = {mask};
    selected_ids = {QStringLiteral("clipboard-paint-a")};
    state.layers = {QVariantMap{{QStringLiteral("id"), selected_ids.first()},
        {QStringLiteral("kind"), QStringLiteral("pixel")}, {QStringLiteral("pixelWidth"), 1},
        {QStringLiteral("pixelHeight"), 1}, {QStringLiteral("paintTiles"), empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}}};
  } else {
    QVector<QByteArray> pixels;
    auto mask_bytes = history_fixture_mask_bytes();
    const auto unicode = QString(80 * 1024, QChar(0x96ea));
    state = initial_paint_history_state(mask_bytes, unicode, &pixels);
    auto first = state.layers[0].toMap();
    auto second = state.layers[1].toMap();
    first.insert(QStringLiteral("id"), selected_ids.first());
    second.insert(QStringLiteral("id"), selected_ids.last());
    state.layers = {first, second};
  }
  const PhotoLayerClipboard clipboard{state, canvas, selected_ids};
  const auto memory_before = process_memory_sample();
  QElapsedTimer timer;
  timer.start();
  const auto bytes = encode_photo_layer_clipboard(clipboard);
  const auto encode_ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
  const auto memory_after = process_memory_sample();
  if (over_limit) {
    constexpr qint64 minimum_base64_bytes =
        ((8192LL * 6400LL + 2LL) / 3LL) * 4LL;
    if (minimum_base64_bytes <= maximum_photo_layer_clipboard_bytes)
      return fail("Clipboard overflow fixture must exceed the configured byte cap");
    if (!bytes.isEmpty()) return fail("Oversized photo layer clipboard was not rejected");
    return write_result({{"scenario", QStringLiteral("clipboard-over-limit")},
        {"encode_ms", encode_ms}, {"result_bytes", bytes.size()},
        {"minimum_base64_bytes", minimum_base64_bytes},
        {"limit_bytes", maximum_photo_layer_clipboard_bytes},
        {"memory_before", memory_json(memory_before)}, {"memory_after", memory_json(memory_after)}}, arguments)
        ? 0 : fail("Artifact unavailable");
  }
  const auto expected = legacy_clipboard_bytes(clipboard);
  if (bytes.isEmpty()) {
    const auto built_recipe = build_edit_recipe_object(clipboard.state);
    const auto validation = validate_edit_recipe_json(
        QString::fromUtf8(QJsonDocument(built_recipe).toJson(QJsonDocument::Compact)));
    std::cerr << "Clipboard encoder returned empty; legacy_bytes_empty=" << expected.isEmpty()
              << "; component_error="
              << built_recipe.value(QStringLiteral("componentObjectTableError")).toString().toStdString()
              << "; text_dimension_error="
              << built_recipe.value(QStringLiteral("textDimensionError")).toString().toStdString()
              << "; recipe_validation=" << validation.errors.join(QStringLiteral(" | ")).toStdString()
              << '\n';
    return fail("Clipboard encoder returned empty for the representative fixture");
  }
  if (expected.isEmpty())
    return fail("Legacy clipboard oracle returned empty for a nonempty encoder result");
  if (bytes != expected)
    return fail("Clipboard bytes differ from the legacy encoding oracle");
  if (!decode_photo_layer_clipboard(bytes)) {
    const auto wrapper = QJsonDocument::fromJson(bytes).object();
    const auto recipe_json = QString::fromUtf8(QJsonDocument(
        wrapper.value(QStringLiteral("recipe")).toObject()).toJson(QJsonDocument::Compact));
    const auto validation = validate_edit_recipe_json(recipe_json);
    std::cerr << "Clipboard decode failed; recipe_validation="
              << validation.errors.join(QStringLiteral(" | ")).toStdString()
              << "; layer_count=" << clipboard.state.layers.size()
              << "; mask_count=" << clipboard.state.mask_stack.size()
              << "; selected_ids=" << clipboard.selected_ids.join(QStringLiteral(", ")).toStdString()
              << '\n';
    return fail("Legacy clipboard bytes were produced but could not be decoded");
  }
  return write_result({{"scenario", QStringLiteral("clipboard-representative")},
      {"encode_ms", encode_ms}, {"result_bytes", bytes.size()}, {"exact_legacy_bytes", true},
      {"memory_before", memory_json(memory_before)}, {"memory_after", memory_json(memory_after)}}, arguments)
      ? 0 : fail("Artifact unavailable");
}

int clipboard_contracts(const QStringList& arguments) {
  QVector<QByteArray> pixel_hashes;
  const auto mask_bytes = history_fixture_mask_bytes();
  const auto unicode = QString(80 * 1024, QChar(0x96ea));
  auto base_state = initial_paint_history_state(mask_bytes, unicode, &pixel_hashes);
  auto first_layer = base_state.layers.at(0).toMap();
  auto second_layer = base_state.layers.at(1).toMap();
  first_layer.insert(QStringLiteral("id"), QStringLiteral("paint-a"));
  second_layer.insert(QStringLiteral("id"), QStringLiteral("paint-b"));
  base_state.layers = {first_layer, second_layer};
  const QSize canvas(kHistoryPaintCanvasWidth, kHistoryPaintCanvasHeight);
  const QStringList selected_ids{QStringLiteral("paint-a"), QStringLiteral("paint-b")};

  // Differentially verify the cheap bound against Qt's serializer, including
  // escaping, multi-unit Unicode, lone surrogates, empty keys and scalar values.
  QString odd_unicode;
  odd_unicode.append(QChar(0xd83c));
  odd_unicode.append(QChar(0xdf0c));
  odd_unicode.append(QChar(0xd800));
  odd_unicode.append(QChar(0x0000));
  const QJsonArray size_cases{
      QJsonObject{{QStringLiteral(""), QStringLiteral("\"\\\n\t")}},
      QJsonObject{{odd_unicode, QJsonArray{odd_unicode, QStringLiteral("雪景"),
          QString(257, QChar('a')), true, false, QJsonValue::Null, -1.25}}},
      QJsonObject{{QStringLiteral("nested"), QJsonObject{
          {QStringLiteral("keys"), QStringLiteral("values")}}}}};
  for (const auto& item : size_cases) {
    const auto serialized_size = QJsonDocument(item.toObject()).toJson(QJsonDocument::Compact).size();
    for (qsizetype budget = 0; budget <= serialized_size + 1; ++budget) {
      auto remaining = budget;
      if (detail::photo_layer_clipboard_json_text_exceeds_budget(item, remaining) &&
          serialized_size <= budget)
        return fail("Clipboard size precheck falsely rejected Qt JSON within its byte cap");
    }
  }
  auto tiny_budget = qsizetype{16};
  if (!detail::photo_layer_clipboard_json_text_exceeds_budget(
          QJsonValue(QString(17, QChar('a'))), tiny_budget))
    return fail("Clipboard size precheck missed a guaranteed oversized string");

  QJsonArray receipts;
  const auto check = [&](const QString& name, const PhotoLayerClipboard& candidate,
                         bool require_decode, bool expect_empty,
                         bool require_direct_probe_parity) {
    const auto legacy = legacy_clipboard_bytes(candidate);
    const auto actual = encode_photo_layer_clipboard(candidate);
    const auto direct_probe = direct_clipboard_candidate_bytes(candidate);
    if (actual != legacy) {
      std::cerr << "Live clipboard encoder differs from legacy oracle for "
                << name.toStdString() << '\n';
      return false;
    }
    const auto direct_probe_matches_legacy = direct_probe == legacy;
    if (require_direct_probe_parity && !direct_probe_matches_legacy) {
      std::cerr << "Shallow direct clipboard probe differs from legacy oracle for "
                << name.toStdString() << "; direct_bytes=" << direct_probe.size()
                << "; legacy_bytes=" << legacy.size()
                << '\n';
      return false;
    }
    if (actual.isEmpty() != expect_empty) {
      std::cerr << "Clipboard contract empty-result parity failed for "
                << name.toStdString() << '\n';
      return false;
    }
    if (require_decode && !decode_photo_layer_clipboard(actual)) {
      std::cerr << "Clipboard contract decode failed for " << name.toStdString() << '\n';
      return false;
    }
    receipts.append(QJsonObject{{QStringLiteral("case"), name},
        {QStringLiteral("exact_legacy_bytes"), actual == legacy},
        {QStringLiteral("direct_probe_matches_legacy"), direct_probe_matches_legacy},
        {QStringLiteral("empty_result"), actual.isEmpty()},
        {QStringLiteral("decode_required"), require_decode},
        {QStringLiteral("encoded_bytes"), actual.size()},
        {QStringLiteral("direct_probe_bytes"), direct_probe.size()},
        {QStringLiteral("live_encoder_bytes"), actual.size()}});
    return true;
  };

  if (!check(QStringLiteral("unicode-paint-mask"),
      PhotoLayerClipboard{base_state, canvas, selected_ids}, true, false, true))
    return fail("Unicode paint and mask clipboard contract failed");

  auto finite_state = base_state;
  finite_state.exposure = -0.125;
  auto finite_snapshot = finite_state.recipe_snapshots.first().toMap();
  finite_snapshot.insert(QStringLiteral("numbers"), QVariantMap{
      {QStringLiteral("minimum"), std::numeric_limits<double>::lowest()},
      {QStringLiteral("maximum"), std::numeric_limits<double>::max()},
      {QStringLiteral("negativeZero"), -0.0},
      {QStringLiteral("signedInteger"), qint64{9007199254740991LL}}});
  finite_state.recipe_snapshots = {finite_snapshot};
  if (!check(QStringLiteral("finite-number-boundaries"),
      PhotoLayerClipboard{finite_state, canvas, selected_ids}, true, false, true))
    return fail("Finite-number clipboard contract failed");

  auto nonfinite_state = base_state;
  auto nonfinite_snapshot = nonfinite_state.recipe_snapshots.first().toMap();
  nonfinite_snapshot.insert(QStringLiteral("numbers"), QVariantMap{
      {QStringLiteral("nan"), std::numeric_limits<double>::quiet_NaN()},
      {QStringLiteral("positiveInfinity"), std::numeric_limits<double>::infinity()},
      {QStringLiteral("negativeInfinity"), -std::numeric_limits<double>::infinity()}});
  nonfinite_state.recipe_snapshots = {nonfinite_snapshot};
  if (!check(QStringLiteral("nonfinite-number-parity"),
      PhotoLayerClipboard{nonfinite_state, canvas, selected_ids}, true, false, true))
    return fail("Nonfinite-number clipboard contract failed");

  auto invalid_style_state = base_state;
  auto invalid_style_layer = invalid_style_state.layers.first().toMap();
  invalid_style_layer.insert(QStringLiteral("layerStyleStack"), QStringLiteral("not-json"));
  invalid_style_state.layers[0] = invalid_style_layer;
  const auto style_error_object = build_edit_recipe_object(invalid_style_state);
  if (!style_error_object.contains(QStringLiteral("componentObjectTableError")) ||
      !check(QStringLiteral("invalid-layer-style"),
          PhotoLayerClipboard{invalid_style_state, canvas, selected_ids}, false, true, true))
    return fail("Invalid-style clipboard rejection changed");

  auto invalid_text_state = base_state;
  auto invalid_text_layer = invalid_text_state.layers.first().toMap();
  invalid_text_layer.insert(QStringLiteral("textDimension"), QVariantMap{});
  invalid_text_state.layers[0] = invalid_text_layer;
  const auto text_error_object = build_edit_recipe_object(invalid_text_state);
  if (!text_error_object.contains(QStringLiteral("textDimensionError")) ||
      !check(QStringLiteral("invalid-text-dimension"),
          PhotoLayerClipboard{invalid_text_state, canvas, selected_ids}, false, true, true))
    return fail("Invalid-text-dimension clipboard rejection changed");

  auto deep_state = base_state;
  QVariant nested(QStringLiteral("leaf"));
  constexpr int nesting_depth = 1030;
  for (int index = 0; index < nesting_depth; ++index)
    nested = QVariantMap{{QStringLiteral("level"), nested}};
  auto deep_snapshot = deep_state.recipe_snapshots.first().toMap();
  deep_snapshot.insert(QStringLiteral("deep"), nested);
  deep_state.recipe_snapshots = {deep_snapshot};
  const auto serialized_deep_recipe = serialize_edit_recipe_json(deep_state);
  QJsonParseError deep_parse_error;
  const auto parsed_deep_recipe = QJsonDocument::fromJson(
      serialized_deep_recipe.toUtf8(), &deep_parse_error);
  std::cerr << "Clipboard deep metadata probe; serialized_nonempty="
            << !serialized_deep_recipe.isEmpty()
            << "; parser_succeeded=" << (deep_parse_error.error == QJsonParseError::NoError)
            << "; parser_error=" << deep_parse_error.errorString().toStdString()
            << '\n';
  receipts.append(QJsonObject{{QStringLiteral("case"), QStringLiteral("deep-metadata-parser-boundary")},
      {QStringLiteral("serialized_nonempty"), !serialized_deep_recipe.isEmpty()},
      {QStringLiteral("legacy_parser_succeeded"), deep_parse_error.error == QJsonParseError::NoError},
      {QStringLiteral("legacy_parse_error"), deep_parse_error.errorString()}});
  static_cast<void>(parsed_deep_recipe);
  if (!check(QStringLiteral("deep-metadata-parser-boundary"),
      PhotoLayerClipboard{deep_state, canvas, selected_ids}, false, false, false))
    return fail("Deep metadata did not preserve legacy clipboard bytes");

  // A parser-rejected deep recipe produces the legacy empty embedded object,
  // even when its pre-parse content alone is above the clipboard byte cap.
  auto deep_oversized_snapshot = deep_snapshot;
  deep_oversized_snapshot.insert(QStringLiteral("oversizedPadding"),
      QString(maximum_photo_layer_clipboard_bytes + 1, QChar('p')));
  deep_state.recipe_snapshots = {deep_oversized_snapshot};
  if (!check(QStringLiteral("deep-oversized-parser-boundary"),
      PhotoLayerClipboard{deep_state, canvas, selected_ids}, false, false, false))
    return fail("Clipboard size precheck bypassed the deep-recipe legacy parser fallback");

  return write_result({{QStringLiteral("scenario"), QStringLiteral("clipboard-encoder-contracts")},
      {QStringLiteral("cases"), receipts}}, arguments) ? 0 : fail("Artifact unavailable");
}

int paint_history_stress(const QStringList& arguments, const QString& directory, bool uniform) {
  const bool baseline = arguments.contains(QStringLiteral("--baseline"));
  const bool burst = arguments.contains(QStringLiteral("--burst"));
  EditorPreviewController controller;
  EditorHistoryStorageTestAccess::stressSetup(controller, directory, baseline);
  const auto cpu_start_ms = process_cpu_ms();
  QElapsedTimer whole_workload;
  whole_workload.start();
  QList<double> admission_ms;
  QList<double> undo_ms;
  QList<double> redo_ms;
  QVector<PaintHistoryReceipt> receipts;
  receipts.reserve(kHistoryPaintRevisionCount + 1);
  QVector<QByteArray> pixel_digests;
  auto mask_bytes = history_fixture_mask_bytes();
  auto repeated_unicode = QString(80 * 1024, QChar(0x96ea));
  const auto mask_payload_bytes = static_cast<qint64>(mask_bytes.size());
  const auto unicode_code_units = repeated_unicode.size();
  auto value = initial_paint_history_state(mask_bytes, repeated_unicode, &pixel_digests, uniform);
  for (int revision = 0; revision <= kHistoryPaintRevisionCount; ++revision) {
    if (revision > 0) {
      value = EditorHistoryStorageTestAccess::current(controller);
      const auto tile_index = (revision - 1) % kHistoryPaintTileCount;
      const auto image = history_fixture_tile(static_cast<quint32>(0x4000 + revision), uniform);
      pixel_digests[tile_index] = hash_image(image);
      PhotoPaintSurface surface;
      QString error;
      if (!surface.load(value.layers.first().toMap(), &error)) return fail("Paint history fixture could not load its previous tile state");
      surface.store(tile_index, 0, image);
      const auto tiles = surface.snapshot();
      for (qsizetype layer_index = 0; layer_index < value.layers.size(); ++layer_index) {
        auto layer = value.layers.at(layer_index).toMap();
        layer.insert(QStringLiteral("paintTiles"), tiles);
        value.layers[layer_index] = layer;
      }
    }
    value.exposure = static_cast<double>(revision) / 100.0;
    value.recipe_snapshots = {history_fixture_snapshot(revision, repeated_unicode)};
    if (revision == kHistoryPaintRevisionCount) {
      EditorHistoryStorageTestAccess::apply(controller, value);
    } else {
      QElapsedTimer timer;
      timer.start();
      EditorHistoryStorageTestAccess::append(controller, value, revision);
      admission_ms.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      if (!burst) EditorHistoryStorageTestAccess::wait(controller);
    }
    const auto actual = EditorHistoryStorageTestAccess::current(controller);
    auto receipt = paint_history_receipt(actual, pixel_digests);
    if (!receipt) return fail("Could not record exact paint history fixture state");
    receipts.append(std::move(*receipt));
  }
  value = {};
  mask_bytes = {};
  repeated_unicode = {};
  EditorHistoryStorageTestAccess::wait(controller);
  const auto capture_stats = EditorHistoryStorageTestAccess::stats(controller);
  const auto capture_memory = process_memory_sample();
  const auto capture_disk_bytes = disk_bytes(capture_stats.value(QStringLiteral("spill_directory")).toString());
  for (int revision = kHistoryPaintRevisionCount - 1; revision >= 0; --revision) {
    QElapsedTimer timer;
    timer.start();
    controller.undo();
    if (!until([&] { return controller.editHistoryCurrentIndex() == revision; }))
      return fail("Paint history undo did not finish through the controller restore path");
    undo_ms.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    if (!exact_paint_history_receipt(controller, receipts.at(revision)))
      return fail("Paint history undo changed the serialized recipe or decoded tile pixels");
  }
  for (int revision = 1; revision <= kHistoryPaintRevisionCount; ++revision) {
    QElapsedTimer timer;
    timer.start();
    controller.redo();
    if (!until([&] { return controller.editHistoryCurrentIndex() == revision; }))
      return fail("Paint history redo did not finish through the controller restore path");
    redo_ms.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    if (!exact_paint_history_receipt(controller, receipts.at(revision)))
      return fail("Paint history redo changed the serialized recipe or decoded tile pixels");
  }
  EditorHistoryStorageTestAccess::wait(controller);
  const auto traversal_memory = process_memory_sample();
  const auto traversal_stats = EditorHistoryStorageTestAccess::stats(controller);
  const auto traversal_disk_bytes = disk_bytes(traversal_stats.value(QStringLiteral("spill_directory")).toString());
  QElapsedTimer clear_timer;
  clear_timer.start();
  EditorHistoryStorageTestAccess::clear(controller);
  EditorHistoryStorageTestAccess::wait(controller);
  QCoreApplication::processEvents();
  const auto clear_ms = static_cast<double>(clear_timer.nsecsElapsed()) / 1e6;
  const auto clear_stats = EditorHistoryStorageTestAccess::stats(controller);
  const auto clear_memory = process_memory_sample();
  const auto clear_disk_bytes = disk_bytes(clear_stats.value(QStringLiteral("spill_directory")).toString());
  if (!exact_paint_history_receipt(controller, receipts.last()))
    return fail("Clearing history changed the active recipe or paint pixels");
  const auto workload_ms = static_cast<double>(whole_workload.nsecsElapsed()) / 1e6;
  const auto cpu_ms = process_cpu_ms() - cpu_start_ms;
  qint64 total_logical_tile_bytes = 0;
  QHash<QByteArray, qint64> unique_tile_payloads;
  for (const auto& receipt : receipts) {
    total_logical_tile_bytes += receipt.unique_tile_payload_utf16_bytes;
    for (std::size_t index = 0; index < receipt.compressed_tile_digests.size(); ++index)
      unique_tile_payloads.insert(receipt.compressed_tile_digests[index],
          receipt.tile_payload_utf16_bytes[index]);
  }
  qint64 unique_logical_tile_bytes = 0;
  for (const auto bytes : unique_tile_payloads) unique_logical_tile_bytes += bytes;
  const QJsonObject result{{"scenario", QStringLiteral("paint-history-retention")},
      {"baseline", baseline}, {"burst", burst}, {"uniform_tile_payloads", uniform},
      {"history_revisions", kHistoryPaintRevisionCount},
      {"paint_layers", 2}, {"paint_tiles_per_layer", kHistoryPaintTileCount},
      {"tile_payload_type", QStringLiteral("base64 QString")},
      {"logical_unique_tile_utf16_bytes_per_revision", receipts.isEmpty()
          ? 0 : receipts.first().unique_tile_payload_utf16_bytes},
      {"logical_history_tile_utf16_bytes_sum", total_logical_tile_bytes},
      {"unique_tile_values_across_history", unique_tile_payloads.size()},
      {"unique_logical_tile_utf16_bytes", unique_logical_tile_bytes},
      {"history_payload_budget_bytes", kHistoryPayloadBudgetBytes},
      {"mask_payload_bytes", mask_payload_bytes},
      {"unicode_code_units_per_snapshot", unicode_code_units},
      {"exact_undo_redo_and_clear_state", true},
      {"admission_ms_samples", sample_array(admission_ms)},
      {"admission_p50_ms", percentile_ms(admission_ms, 0.50)},
      {"admission_p95_ms", percentile_ms(admission_ms, 0.95)},
      {"undo_ms_samples", sample_array(undo_ms)},
      {"undo_p50_ms", percentile_ms(undo_ms, 0.50)},
      {"undo_p95_ms", percentile_ms(undo_ms, 0.95)},
      {"redo_ms_samples", sample_array(redo_ms)},
      {"redo_p50_ms", percentile_ms(redo_ms, 0.50)},
      {"redo_p95_ms", percentile_ms(redo_ms, 0.95)},
      {"whole_workload_ms", workload_ms}, {"whole_workload_process_cpu_ms", cpu_ms},
      {"clear_collect_ms", clear_ms},
      {"capture_storage", QJsonObject::fromVariantMap(capture_stats)},
      {"capture_spill_disk_bytes", capture_disk_bytes}, {"capture_memory", memory_json(capture_memory)},
      {"traversal_storage", QJsonObject::fromVariantMap(traversal_stats)},
      {"traversal_spill_disk_bytes", traversal_disk_bytes}, {"traversal_memory", memory_json(traversal_memory)},
      {"clear_storage", QJsonObject::fromVariantMap(clear_stats)},
      {"clear_spill_disk_bytes", clear_disk_bytes}, {"clear_memory", memory_json(clear_memory)}};
  return write_result(result, arguments) ? 0 : fail("Artifact unavailable");
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!directory.isValid()) return fail("Temporary storage unavailable");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", directory.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  if (app.arguments().contains(QStringLiteral("--clipboard-stress")))
    return clipboard_stress(false, app.arguments());
  if (app.arguments().contains(QStringLiteral("--clipboard-limit")))
    return clipboard_stress(true, app.arguments());
  if (app.arguments().contains(QStringLiteral("--clipboard-contracts")))
    return clipboard_contracts(app.arguments());
  if (app.arguments().contains(QStringLiteral("--paint-stress")))
    return paint_history_stress(app.arguments(), directory.path(), false);
  if (app.arguments().contains(QStringLiteral("--paint-uniform-stress")))
    return paint_history_stress(app.arguments(), directory.path(), true);
  if (app.arguments().contains("--stress")) {
    const bool baseline = app.arguments().contains("--baseline");
    const bool burst = app.arguments().contains("--burst");
    EditorPreviewController controller;
    EditorHistoryStorageTestAccess::stressSetup(controller, directory.path(), baseline);
    const auto revision = [](int i) {
      EditRecipeState value;
      value.exposure = static_cast<double>(i) / 100.0;
      value.recipe_snapshots = {QVariantMap{{"id", "nested-payload"}, {"binary", QByteArray(4 * 1024 * 1024, static_cast<char>(i))}}};
      return value;
    };
    QList<double> admission_ms;
    for (int i = 0; i < 64; ++i) {
      const auto value = revision(i);
      QElapsedTimer timer; timer.start();
      EditorHistoryStorageTestAccess::append(controller, value, i);
      admission_ms.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      if (!burst) EditorHistoryStorageTestAccess::wait(controller);
    }
    EditorHistoryStorageTestAccess::apply(controller, revision(64));
    EditorHistoryStorageTestAccess::wait(controller);
    const auto resident_revisions = EditorHistoryStorageTestAccess::residentCount(controller);
    const auto stats = EditorHistoryStorageTestAccess::stats(controller);
    struct rusage captured_usage{}; getrusage(RUSAGE_SELF, &captured_usage);
    qint64 capture_peak = static_cast<qint64>(captured_usage.ru_maxrss);
#if !defined(Q_OS_MACOS)
    capture_peak *= 1024;
#endif
    for (int i = 63; i >= 0; --i) {
      controller.undo();
      if (!until([&] { return controller.editHistoryCurrentIndex() == i; }) ||
          EditorHistoryStorageTestAccess::current(controller).recipe_snapshots != revision(i).recipe_snapshots)
        return fail("Stress undo failed exact restoration");
    }
    for (int i = 1; i <= 64; ++i) {
      controller.redo();
      if (!until([&] { return controller.editHistoryCurrentIndex() == i; }) ||
          EditorHistoryStorageTestAccess::current(controller).recipe_snapshots != revision(i).recipe_snapshots)
        return fail("Stress redo failed exact restoration");
    }
    EditorHistoryStorageTestAccess::wait(controller);
    struct rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    qint64 peak = static_cast<qint64>(usage.ru_maxrss);
#if !defined(Q_OS_MACOS)
    peak *= 1024;
#endif
    std::sort(admission_ms.begin(), admission_ms.end());
    const auto p95 = admission_ms.at(60);
    const QJsonObject result{{"baseline", baseline}, {"burst", burst}, {"revisions", 64}, {"payload_bytes", 4 * 1024 * 1024},
        {"admission_p95_ms", p95}, {"resident_revisions_after_capture", resident_revisions},
        {"peak_rss_bytes", peak}, {"capture_peak_rss_bytes", capture_peak}, {"exact_undo_redo", true}, {"storage", QJsonObject::fromVariantMap(stats)}};
    const auto output = app.arguments().indexOf("--output");
    if (output >= 0 && output + 1 < app.arguments().size()) {
      QFile file(app.arguments().at(output + 1));
      if (!file.open(QIODevice::WriteOnly)) return fail("Artifact unavailable");
      file.write(QJsonDocument(result).toJson());
    }
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return 0;
  }
  EditorPreviewController controller;
  EditorHistoryStorageTestAccess::setup(controller, directory.path());
  for (int i = 0; i < 12; ++i) EditorHistoryStorageTestAccess::append(controller, state(i), i);
  EditorHistoryStorageTestAccess::apply(controller, state(12));
  EditorHistoryStorageTestAccess::wait(controller);
  if (controller.editHistoryCurrentIndex() != 12 || EditorHistoryStorageTestAccess::residentCount(controller) != 2 ||
      EditorHistoryStorageTestAccess::stats(controller).value("resident_bytes").toLongLong() > 8 * 1024 * 1024)
    return fail("History did not release older raw states or enforce its store budget");

  const auto before_no_op = EditorHistoryStorageTestAccess::generation(controller);
  if (!controller.restoreEditHistoryIndex(12) || EditorHistoryStorageTestAccess::generation(controller) != before_no_op)
    return fail("Restoring the current row should not reapply or persist the recipe");
  if (controller.editHistoryEntries().size() != 13 || !until([&] { return EditorHistoryStorageTestAccess::formatReady(controller); }))
    return fail("Background history descriptions did not complete");
  if (controller.editHistoryEntries().at(1).toMap().value("label").toString() != "Revision 0")
    return fail("History label changed during background formatting");
  if (!controller.restoreEditHistoryIndex(2) || controller.editHistoryCurrentIndex() != 12 || !exact(controller, 12))
    return fail("Pending restore mutated history before its payload was available");
  if (!until([&] { return controller.editHistoryCurrentIndex() == 2; }) || !exact(controller, 2))
    return fail("Spilled history jump did not restore exact state");
  controller.redo();
  if (!until([&] { return controller.editHistoryCurrentIndex() == 3; }) || !exact(controller, 3))
    return fail("Redo after a disk restore did not preserve state");
  controller.undo(); controller.undo();
  if (!until([&] { return controller.editHistoryCurrentIndex() == 1; }) || !exact(controller, 1))
    return fail("Repeated undo intent or labels were lost");

  EditorHistoryStorageTestAccess::unpin(controller);
  if (!controller.setEditHistoryPreviewIndex(0)) return fail("Spilled hover was rejected");
  controller.setEditHistoryPreviewIndex(-1);
  settle();
  if (EditorHistoryStorageTestAccess::preview(controller) || !exact(controller, 1) || controller.editHistoryCurrentIndex() != 1)
    return fail("Canceled hover changed the active edit or published late");

  if (!controller.restoreEditHistoryIndex(12) || !until([&] { return controller.editHistoryCurrentIndex() == 12; }))
    return fail("Could not restore timeline tip");
  EditorHistoryStorageTestAccess::unpin(controller);
  const auto id = QString::fromLatin1(QCryptographicHash::hash(bytes(0), QCryptographicHash::Sha256).toHex());
  const auto path = EditorHistoryStorageTestAccess::stats(controller).value("spill_directory").toString() + "/" + id + ".blob";
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes(99)) != bytes(99).size())
    return fail("Could not construct checksum failure");
  file.close();
  if (!controller.restoreEditHistoryIndex(0) || !until([&] { return !EditorHistoryStorageTestAccess::pending(controller); }))
    return fail("Corrupt restore did not finish with an error");
  if (controller.editHistoryCurrentIndex() != 12 || !exact(controller, 12) || !controller.status().contains("checksum"))
    return fail("Corrupt history advanced the cursor or damaged current edits");
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes(0)) != bytes(0).size()) return fail("Repair failed");
  file.close();
  if (!controller.restoreEditHistoryIndex(0) || !until([&] { return controller.editHistoryCurrentIndex() == 0; }) || !exact(controller, 0))
    return fail("A repaired payload could not be retried");

  EditorHistoryStorageTestAccess::unpin(controller);
  controller.restoreEditHistoryIndex(6);
  controller.setExposure(0.77);
  settle();
  if (controller.exposure() != 0.77 || controller.editHistoryCurrentIndex() == 6)
    return fail("A stale restore overwrote a newer edit");
  EditorHistoryStorageTestAccess::unpin(controller);
  controller.restoreEditHistoryIndex(0);
  controller.loadAsset("history-storage-b", {}, {}, {}, {}, {}, {});
  settle();
  if (controller.exposure() != 0.0 || controller.editHistoryCurrentIndex() != 0 || EditorHistoryStorageTestAccess::preview(controller))
    return fail("A late history result crossed an asset switch");
  {
    EditorPreviewController paint_controller;
    EditorHistoryStorageTestAccess::setup(paint_controller, directory.filePath("small-paint-history"));
    PhotoPaintSurface surface;
    surface.size = QSize(64, 64);
    surface.color_space = QColorSpace(QColorSpace::SRgbLinear);
    surface.payload = empty_photo_paint_tiles(surface.color_space);
    QImage tile(surface.size, QImage::Format_RGBA32FPx4_Premultiplied);
    tile.fill(Qt::red);
    surface.store(0, 0, tile);
    EditRecipeState painted;
    painted.layers = {QVariantMap{{"id", "small-paint"}, {"kind", "pixel"},
        {"pixelWidth", 64}, {"pixelHeight", 64}, {"canvasWidth", 64}, {"canvasHeight", 64},
        {"paintTiles", surface.snapshot()}, {"enabled", true}, {"opacity", 1.0}}};
    EditorHistoryStorageTestAccess::append(paint_controller, painted, 0);
    painted.exposure = 0.25;
    EditorHistoryStorageTestAccess::apply(paint_controller, painted);
    const auto entries = paint_controller.editHistoryEntries();
    if (entries.size() != 2 || entries.at(1).toMap().value("detail") != "Preparing edit details")
      return fail("Small compressed paint history should defer detailed diffing to a worker");
    if (!until([&] { return EditorHistoryStorageTestAccess::formatReady(paint_controller); }) ||
        paint_controller.editHistoryEntries().at(1).toMap().value("label") != "Revision 0")
      return fail("Deferred small-paint history lost the detailed label");
  }
  std::cout << "History ownership, worker restore, cursor safety, labels, hover cancellation, and stale results passed\n";
  return 0;
}
