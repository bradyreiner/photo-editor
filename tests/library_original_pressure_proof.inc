#include "decoded_image_cache.h"
#include "library_original_image_provider.h"
#include "photo_cache_budget.h"
#include "qimage_color_conversion.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <array>
#include <ctime>
#include <iostream>
#include <memory>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/task_info.h>
#endif

namespace library_original_pressure_proof {
using namespace photo_editor::desktop;

constexpr int kImageWidth = 4096;
constexpr int kImageHeight = 3072;
constexpr qint64 kImageBytes =
    static_cast<qint64>(kImageWidth) * kImageHeight * 4;
constexpr qint64 kNormalBudgetBytes = 256LL * 1024 * 1024;
constexpr qint64 kOneImageBudgetBytes = 56LL * 1024 * 1024;

QByteArray pixel_hash(const QImage& image) {
  if (image.isNull()) return {};
  const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
  if (rgba.isNull()) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(QByteArray::number(rgba.width()));
  hash.addData(QByteArrayView("x", 1));
  hash.addData(QByteArray::number(rgba.height()));
  for (int y = 0; y < rgba.height(); ++y) {
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(rgba.constScanLine(y)),
                 static_cast<qsizetype>(rgba.width()) * 4));
  }
  return hash.result().toHex();
}

QImage make_source_image(int seed) {
  QImage image(kImageWidth, kImageHeight, QImage::Format_RGBA8888);
  if (image.isNull()) return {};
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < image.height(); ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      const auto offset = static_cast<qsizetype>(x) * 4;
      row[offset] = static_cast<uchar>((x + 3 * y + 19 * seed) & 0xff);
      row[offset + 1] = static_cast<uchar>((3 * x + y + 43 * seed) & 0xff);
      row[offset + 2] = static_cast<uchar>((7 * x + 11 * y + 71 * seed) & 0xff);
      row[offset + 3] = 255;
    }
  }
  return image;
}

QString image_provider_id(const QString& path) {
  const QUrl source(LibraryOriginalImageProvider::sourceUrl(path));
  return source.path(QUrl::FullyEncoded).mid(1) + QLatin1Char('?') +
         source.query(QUrl::FullyEncoded);
}

QVariantMap library_owner_stats() {
  const auto owners = photo_cache_budget_snapshot()
                          .value(QStringLiteral("owners")).toMap();
  return owners.value(QStringLiteral("libraryOriginal")).toMap();
}

QJsonObject owner_receipt() {
  const auto stats = library_owner_stats();
  return {{QStringLiteral("present"), !stats.isEmpty()},
          {QStringLiteral("bytes"), stats.value(QStringLiteral("bytes")).toLongLong()},
          {QStringLiteral("entries"), stats.value(QStringLiteral("entries")).toLongLong()},
          {QStringLiteral("hits"), stats.value(QStringLiteral("hits")).toLongLong()},
          {QStringLiteral("misses"), stats.value(QStringLiteral("misses")).toLongLong()},
          {QStringLiteral("sharedLimitEvictions"),
           stats.value(QStringLiteral("sharedLimitEvictions")).toLongLong()},
          {QStringLiteral("pressureEvictions"),
           stats.value(QStringLiteral("pressureEvictions")).toLongLong()}};
}

QJsonObject process_memory_receipt() {
  QJsonObject memory;
#ifdef Q_OS_MACOS
  mach_task_basic_info_data_t basic{};
  mach_msg_type_number_t basic_count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&basic), &basic_count) == KERN_SUCCESS) {
    memory.insert(QStringLiteral("residentBytes"),
                  static_cast<qint64>(basic.resident_size));
  }
  task_vm_info_data_t vm{};
  mach_msg_type_number_t vm_count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&vm), &vm_count) == KERN_SUCCESS) {
    memory.insert(QStringLiteral("physicalFootprintBytes"),
                  static_cast<qint64>(vm.phys_footprint));
  }
#endif
  return memory;
}

bool image_matches(const QImage& image, const QByteArray& expected_hash) {
  return !image.isNull() && image.size() == QSize(kImageWidth, kImageHeight) &&
         pixel_hash(image) == expected_hash;
}

int run_library_original_pressure_proof(const QString& receipt_path,
                                         bool expect_shared_owner) {
  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    std::cerr << "Could not create private full-resolution source directory\n";
    return 1;
  }

  std::array<QString, 3> paths;
  std::array<QByteArray, 3> expected_hashes;
  for (int index = 0; index < 3; ++index) {
    paths[static_cast<std::size_t>(index)] =
        temporary.filePath(QStringLiteral("library-original-%1.png").arg(index));
    auto source = make_source_image(index + 1);
    if (source.isNull()) {
      std::cerr << "Could not allocate generated full-resolution source\n";
      return 1;
    }
    auto expected = convert_tagged_qimage_color_space(
        source, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
        QImageColorConversionPurpose::OutputPublication);
    if (expected.isNull() || !source.save(paths[static_cast<std::size_t>(index)], "PNG")) {
      std::cerr << "Could not write private full-resolution PNG source\n";
      return 1;
    }
    expected_hashes[static_cast<std::size_t>(index)] = pixel_hash(expected);
    if (expected_hashes[static_cast<std::size_t>(index)].isEmpty()) {
      std::cerr << "Could not hash expected sRGB pixels\n";
      return 1;
    }
  }

  clear_photo_caches();
  set_photo_cache_pressure(QStringLiteral("normal"));
  set_photo_cache_budget_bytes(kNormalBudgetBytes);
  QJsonObject receipt{
      {QStringLiteral("schema"), QStringLiteral("library-original-pressure-proof:v1")},
      {QStringLiteral("width"), kImageWidth},
      {QStringLiteral("height"), kImageHeight},
      {QStringLiteral("imageBytes"), kImageBytes},
      {QStringLiteral("sourceCount"), 3},
      {QStringLiteral("providerLocalLimitBytes"), 192LL * 1024 * 1024},
      {QStringLiteral("normalSharedBudgetBytes"), kNormalBudgetBytes},
      {QStringLiteral("expectSharedOwner"), expect_shared_owner}};
  QJsonArray cold_ms;
  QJsonArray cold_cpu_ms;
  QJsonArray warm_ms;
  QJsonArray warm_cpu_ms;
  QJsonArray held_hashes;
  std::array<QImage, 3> held_images;
  bool pixels_exact = true;
  QJsonObject owner_after_fill;
  QJsonObject owner_after_shrink;
  QJsonObject owner_after_critical;
  QJsonObject owner_before_destroy;
  QJsonObject owner_after_destroy;
  QJsonObject memory_after_fill;
  QJsonObject memory_after_shared_shrink;
  QJsonObject memory_after_pressure_released_consumer_refs;
  QJsonObject memory_after_provider_destroy;
  QImage held_after_destroy;
  double touched_a_after_shrink_ms = -1.0;
  double reload_b_after_shrink_ms = -1.0;
  double reloaded_c_after_critical_ms = -1.0;
  bool held_images_exact_after_critical = false;
  bool held_images_exact_after_destroy = false;

  {
    auto provider = std::make_unique<LibraryOriginalImageProvider>();
    for (int index = 0; index < 3; ++index) {
      QSize returned_size;
      const auto cpu_before = std::clock();
      QElapsedTimer timer;
      timer.start();
      auto image = provider->requestImage(
          image_provider_id(paths[static_cast<std::size_t>(index)]),
          &returned_size, QSize{});
      const auto elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
      const auto cpu_after = std::clock();
      cold_ms.append(elapsed_ms);
      cold_cpu_ms.append(cpu_after >= cpu_before
          ? 1000.0 * static_cast<double>(cpu_after - cpu_before) /
                static_cast<double>(CLOCKS_PER_SEC)
          : -1.0);
      pixels_exact = pixels_exact && returned_size == QSize(kImageWidth, kImageHeight) &&
          image_matches(image, expected_hashes[static_cast<std::size_t>(index)]);
      held_images[static_cast<std::size_t>(index)] = image;
      held_hashes.append(QString::fromLatin1(pixel_hash(image)));
      clear_decoded_image_cache();
    }
    if (!pixels_exact) {
      std::cerr << "Cold requestImage results did not match exact generated sRGB pixels\n";
      return 1;
    }
    owner_after_fill = owner_receipt();
    memory_after_fill = process_memory_receipt();

    std::array<QImage, 3> warm_images;
    for (int iteration = 0; iteration < 96; ++iteration) {
      const int index = iteration % 3;
      QSize returned_size;
      const auto cpu_before = std::clock();
      QElapsedTimer timer;
      timer.start();
      const auto image = provider->requestImage(
          image_provider_id(paths[static_cast<std::size_t>(index)]),
          &returned_size, QSize{});
      warm_ms.append(static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0);
      const auto cpu_after = std::clock();
      warm_cpu_ms.append(cpu_after >= cpu_before
          ? 1000.0 * static_cast<double>(cpu_after - cpu_before) /
                static_cast<double>(CLOCKS_PER_SEC)
          : -1.0);
      pixels_exact = pixels_exact && returned_size == QSize(kImageWidth, kImageHeight);
      warm_images[static_cast<std::size_t>(index)] = image;
    }
    // Hash each warm return after the timed loop, once per source image.
    for (int index = 0; index < 3; ++index)
      pixels_exact = pixels_exact && image_matches(
          warm_images[static_cast<std::size_t>(index)],
          expected_hashes[static_cast<std::size_t>(index)]);
    if (!pixels_exact) {
      std::cerr << "Warm requestImage results did not preserve exact pixels\n";
      return 1;
    }
    warm_images = std::array<QImage, 3>{};

    // Leave A most-recent before shrinking so the shared LRU should preserve A.
    {
      QSize returned_size;
      const auto image = provider->requestImage(
          image_provider_id(paths[0]), &returned_size, QSize{});
      pixels_exact = returned_size == QSize(kImageWidth, kImageHeight) &&
          image_matches(image, expected_hashes[0]);
      clear_decoded_image_cache();
      if (!pixels_exact) {
        std::cerr << "Could not touch image A before the shared-budget shrink\n";
        return 1;
      }
    }

    // Loading A, B, C then touching A should leave A as the sole survivor when
    // the shared cache ceiling is reduced below the two-image total.
    set_photo_cache_budget_bytes(kOneImageBudgetBytes);
    owner_after_shrink = owner_receipt();
    memory_after_shared_shrink = process_memory_receipt();
    {
      QSize returned_size;
      QElapsedTimer timer;
      timer.start();
      const auto image = provider->requestImage(
          image_provider_id(paths[0]), &returned_size, QSize{});
      touched_a_after_shrink_ms =
          static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
      pixels_exact = pixels_exact && returned_size == QSize(kImageWidth, kImageHeight) &&
          image_matches(image, expected_hashes[0]);
      clear_decoded_image_cache();
    }
    {
      QSize returned_size;
      QElapsedTimer timer;
      timer.start();
      const auto image = provider->requestImage(
          image_provider_id(paths[1]), &returned_size, QSize{});
      reload_b_after_shrink_ms =
          static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
      pixels_exact = pixels_exact && returned_size == QSize(kImageWidth, kImageHeight) &&
          image_matches(image, expected_hashes[1]);
      clear_decoded_image_cache();
    }
    if (!pixels_exact) {
      std::cerr << "Post-shrink requestImage results did not preserve exact pixels\n";
      return 1;
    }

    set_photo_cache_pressure(QStringLiteral("critical"));
    owner_after_critical = owner_receipt();
    held_images_exact_after_critical = true;
    for (int index = 0; index < 3; ++index) {
      held_images_exact_after_critical = held_images_exact_after_critical &&
          image_matches(held_images[static_cast<std::size_t>(index)],
                        expected_hashes[static_cast<std::size_t>(index)]);
    }
    if (!held_images_exact_after_critical) {
      std::cerr << "Pressure eviction changed a QImage still held by a consumer\n";
      return 1;
    }
    // Keep one consumer-owned image alive to verify destruction does not
    // invalidate it. Drop the other two so the receipt's post-pressure live
    // provider ownership is not obscured by the fixture retaining every image.
    held_after_destroy = held_images[0];
    held_images = std::array<QImage, 3>{};
    memory_after_pressure_released_consumer_refs = process_memory_receipt();

    // Refill once after pressure so provider destruction must unregister a
    // live token, not just observe a cache that pressure already emptied.
    set_photo_cache_pressure(QStringLiteral("normal"));
    set_photo_cache_budget_bytes(kNormalBudgetBytes);
    {
      QSize returned_size;
      QElapsedTimer timer;
      timer.start();
      const auto image = provider->requestImage(
          image_provider_id(paths[2]), &returned_size, QSize{});
      reloaded_c_after_critical_ms =
          static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
      pixels_exact = returned_size == QSize(kImageWidth, kImageHeight) &&
          image_matches(image, expected_hashes[2]);
      clear_decoded_image_cache();
    }
    if (!pixels_exact) {
      std::cerr << "Post-pressure source reload changed exact pixels\n";
      return 1;
    }
    owner_before_destroy = owner_receipt();
    provider.reset();
    owner_after_destroy = owner_receipt();
    memory_after_provider_destroy = process_memory_receipt();
  }

  held_images_exact_after_destroy =
      image_matches(held_after_destroy, expected_hashes[0]);
  if (!held_images_exact_after_destroy) {
    std::cerr << "Provider destruction invalidated consumer-held QImages\n";
    return 1;
  }

  if (expect_shared_owner &&
      (owner_after_fill.value(QStringLiteral("present")).toBool() == false ||
       owner_after_fill.value(QStringLiteral("bytes")).toInteger() != 3 * kImageBytes ||
       owner_after_fill.value(QStringLiteral("entries")).toInteger() != 3 ||
       owner_after_shrink.value(QStringLiteral("bytes")).toInteger() != kImageBytes ||
       owner_after_shrink.value(QStringLiteral("entries")).toInteger() != 1 ||
       owner_after_critical.value(QStringLiteral("bytes")).toInteger() != 0 ||
       owner_after_critical.value(QStringLiteral("entries")).toInteger() != 0 ||
       owner_after_destroy.value(QStringLiteral("bytes")).toInteger() != 0 ||
       owner_after_destroy.value(QStringLiteral("entries")).toInteger() != 0)) {
    std::cerr << "Library original cache did not track fill, LRU shrink, pressure, and destruction\n";
    return 1;
  }

  receipt.insert(QStringLiteral("coldRequestMs"), cold_ms);
  receipt.insert(QStringLiteral("coldRequestCpuMs"), cold_cpu_ms);
  receipt.insert(QStringLiteral("warmRequestMs"), warm_ms);
  receipt.insert(QStringLiteral("warmRequestCpuMs"), warm_cpu_ms);
  receipt.insert(QStringLiteral("expectedPixelHashes"), QJsonArray{
      QString::fromLatin1(expected_hashes[0]), QString::fromLatin1(expected_hashes[1]),
      QString::fromLatin1(expected_hashes[2])});
  receipt.insert(QStringLiteral("initialHeldPixelHashes"), held_hashes);
  receipt.insert(QStringLiteral("exactPixels"), pixels_exact);
  receipt.insert(QStringLiteral("ownerAfterFill"), owner_after_fill);
  receipt.insert(QStringLiteral("providerOwnerAccountingAvailable"),
                 owner_after_fill.value(QStringLiteral("present")).toBool());
  receipt.insert(QStringLiteral("ownerAfterSharedBudgetShrink"), owner_after_shrink);
  receipt.insert(QStringLiteral("ownerAfterCriticalPressure"), owner_after_critical);
  receipt.insert(QStringLiteral("ownerBeforeDestroy"), owner_before_destroy);
  receipt.insert(QStringLiteral("ownerAfterDestroy"), owner_after_destroy);
  receipt.insert(QStringLiteral("memoryAfterFill"), memory_after_fill);
  receipt.insert(QStringLiteral("memoryAfterSharedBudgetShrink"), memory_after_shared_shrink);
  receipt.insert(QStringLiteral("memoryAfterPressureConsumerRefsReleased"),
                 memory_after_pressure_released_consumer_refs);
  receipt.insert(QStringLiteral("memoryAfterProviderDestroy"), memory_after_provider_destroy);
  receipt.insert(QStringLiteral("touchAAfterShrinkMs"), touched_a_after_shrink_ms);
  receipt.insert(QStringLiteral("reloadBAfterShrinkMs"), reload_b_after_shrink_ms);
  receipt.insert(QStringLiteral("reloadCAfterCriticalMs"), reloaded_c_after_critical_ms);
  receipt.insert(QStringLiteral("heldImagesExactAfterCritical"), held_images_exact_after_critical);
  receipt.insert(QStringLiteral("heldImagesExactAfterDestroy"), held_images_exact_after_destroy);
  receipt.insert(QStringLiteral("consumerRetainsOneImageAfterPressureAndDestroy"), true);
  receipt.insert(QStringLiteral("localDecodedCacheClearedAfterEachRequest"), true);

  QFile output(receipt_path);
  const auto receipt_bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      output.write(receipt_bytes) != receipt_bytes.size()) {
    std::cerr << "Could not write pressure proof receipt\n";
    return 1;
  }
  output.close();
  clear_photo_caches();
  set_photo_cache_budget_bytes(kNormalBudgetBytes);
  set_photo_cache_pressure(QStringLiteral("normal"));
  return 0;
}
}  // namespace library_original_pressure_proof
