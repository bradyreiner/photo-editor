#include "recipe_asset_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) {
    return fail("recipe asset test requires a temporary root");
  }

  photo_editor::desktop::RecipeAssetStore store(
      root.path());
  const QByteArray payload(
      "mask-payload\0with-binary",
      24);
  const auto first = store.put(payload);
  const auto second = store.put(payload);
  if (!first.ok() || !second.ok() ||
      first.asset != second.asset ||
      !first.asset->value.starts_with("sha256:")) {
    return fail("identical payloads must resolve to one canonical asset");
  }
  QString read_error;
  const auto restored =
      store.get(*first.asset, &read_error);
  if (!restored.has_value() ||
      *restored != payload ||
      !read_error.isEmpty()) {
    return fail("stored recipe assets must round-trip exact bytes");
  }
  const QVariantMap variant_payload{
      {QStringLiteral("bytes"), payload},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("signed"),
       QVariant::fromValue<qlonglong>(-42)},
      {QStringLiteral("unsigned"),
       QVariant::fromValue<qulonglong>(
           9'007'199'254'740'993ULL)},
      {QStringLiteral("values"),
       QVariantList{
           0.25,
           QStringLiteral("mask"),
           QVariantMap{
               {QStringLiteral("nested"),
                QByteArray("\0x", 2)},
           },
       }},
  };
  const auto variant_asset =
      store.putVariant(variant_payload);
  const auto restored_variant =
      variant_asset.ok()
          ? store.getVariant(
                *variant_asset.asset,
                &read_error)
          : std::nullopt;
  if (!restored_variant.has_value() ||
      restored_variant->toMap() != variant_payload) {
    return fail("tagged recipe variants must preserve nested binary and integer types");
  }
  const auto non_finite =
      store.putVariant(
          QVariantMap{
              {QStringLiteral("invalid"),
               std::numeric_limits<double>::infinity()},
          });
  if (non_finite.ok() ||
      !non_finite.error.contains(
          QStringLiteral("non-finite"))) {
    return fail("non-finite variant payloads must fail closed");
  }

  auto invalid = *first.asset;
  invalid.value = "asset:mutable-name";
  if (store.get(invalid, &read_error).has_value() ||
      read_error.isEmpty()) {
    return fail("non-canonical asset identifiers must fail closed");
  }

  const auto digest =
      QString::fromStdString(first.asset->value)
          .mid(7);
  const auto stored_path =
      QDir(root.path())
          .filePath(
              QStringLiteral("sha256/%1/%2.bin")
                  .arg(digest.left(2), digest));
  QFile tampered(stored_path);
  if (!tampered.open(
          QIODevice::WriteOnly |
          QIODevice::Truncate) ||
      tampered.write("tampered") < 0) {
    return fail("recipe asset test must be able to corrupt its fixture");
  }
  tampered.close();
  if (store.get(*first.asset, &read_error).has_value() ||
      !read_error.contains(
          QStringLiteral("SHA-256"))) {
    return fail("tampered recipe assets must fail integrity verification");
  }

  return EXIT_SUCCESS;
}
