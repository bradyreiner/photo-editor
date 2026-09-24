#include "photo_editor/pipeline/color_profile_asset.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

std::vector<std::byte> bytes(std::string_view value) {
  std::vector<std::byte> result;
  result.reserve(value.size());
  for (const auto character : value) {
    result.push_back(static_cast<std::byte>(character));
  }
  return result;
}

}  // namespace

int main() {
  using photo_editor::pipeline::IccProfileAssetReference;
  using photo_editor::pipeline::IccProfileAssetRegistry;
  using photo_editor::pipeline::IccProfileAssetStatus;

  IccProfileAssetRegistry registry;
  const auto unconfigured = registry.resolve({});
  if (unconfigured.status != IccProfileAssetStatus::Unconfigured ||
      unconfigured.ready() || !unconfigured.payload_bytes().empty()) {
    return fail("an absent profile reference should remain explicitly unconfigured");
  }

  const auto payload = bytes("immutable-profile-payload");
  const auto payload_hash = photo_editor::pipeline::color_payload_sha256(payload);
  const IccProfileAssetReference reference{
      .asset_id = "icc:test-profile",
      .payload_sha256 = payload_hash,
      .display_name = "Test RGB",
      .source_uri = {},
      .source_kind = "test-fixture",
      .byte_size = payload.size(),
  };
  const auto registered = registry.register_payload(reference, payload, "memory:test-profile");
  if (!registered.ready() || registered.reference.asset_id != reference.asset_id ||
      registered.reference.payload_sha256 != payload_hash ||
      registered.payload_bytes().size() != payload.size()) {
    return fail("a valid profile payload should register with immutable identity");
  }

  const auto cached = registry.resolve(reference);
  if (!cached.ready() || cached.payload != registered.payload ||
      cached.resolved_uri != "memory:test-profile") {
    return fail("profile resolution should share the immutable registered payload");
  }

  auto changed_reference = reference;
  changed_reference.payload_sha256 = std::string(64, '0');
  const auto changed = registry.resolve(changed_reference);
  if (changed.status != IccProfileAssetStatus::HashMismatch || changed.ready() ||
      changed.failure_reason != "icc_profile_asset_cached_sha256_mismatch") {
    return fail("a changed expected hash must never reuse a cached payload");
  }

  auto wrong_size_reference = reference;
  ++wrong_size_reference.byte_size;
  const auto wrong_size = registry.register_payload(wrong_size_reference, payload);
  if (wrong_size.status != IccProfileAssetStatus::SizeMismatch || wrong_size.ready()) {
    return fail("a persisted byte-size mismatch should fail explicitly");
  }

  std::array<std::future<bool>, 8> concurrent_resolutions;
  for (auto& future : concurrent_resolutions) {
    future = std::async(std::launch::async, [&registry, reference, registered]() {
      const auto result = registry.resolve(reference);
      return result.ready() && result.payload == registered.payload;
    });
  }
  for (auto& future : concurrent_resolutions) {
    if (!future.get()) {
      return fail("concurrent resolution should share the same immutable payload");
    }
  }

  const auto test_root = std::filesystem::temp_directory_path() /
                         ("photo-editor-color-profile-asset-test-" +
                          std::to_string(
                              std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()));
  std::filesystem::create_directories(test_root);
  const auto temporary_path = test_root / "source-profile.icc";
  {
    std::ofstream stream(temporary_path, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(payload.data()),
        static_cast<std::streamsize>(payload.size()));
  }
  IccProfileAssetRegistry file_registry;
  const auto imported = file_registry.import_file(temporary_path.string(), "Imported Test RGB");
  if (!imported.ready() || imported.reference.asset_id != "icc:" + payload_hash ||
      imported.reference.payload_sha256 != payload_hash ||
      imported.reference.byte_size != payload.size() ||
      imported.reference.source_uri != temporary_path.string()) {
    std::filesystem::remove_all(test_root);
    return fail("file import should derive a stable content identity and source metadata");
  }

  const auto persisted_reference = imported.reference;
  file_registry.clear();
  const auto reloaded = file_registry.resolve(persisted_reference);
  if (!reloaded.ready() || reloaded.reference.payload_sha256 != payload_hash ||
      reloaded.resolved_uri != temporary_path.string()) {
    std::filesystem::remove_all(test_root);
    return fail("a persisted reference should reload and verify its source payload");
  }

  const auto replacement_payload = bytes("changed-profile-payload");
  {
    std::ofstream stream(temporary_path, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(replacement_payload.data()),
        static_cast<std::streamsize>(replacement_payload.size()));
  }
  file_registry.clear();
  const auto changed_file = file_registry.resolve(persisted_reference);
  if (changed_file.status != IccProfileAssetStatus::HashMismatch || changed_file.ready()) {
    std::filesystem::remove_all(test_root);
    return fail("a profile file changed after import must fail its persisted hash contract");
  }
  std::filesystem::remove(temporary_path);

  auto missing_reference = persisted_reference;
  missing_reference.source_uri = temporary_path.string();
  const auto missing = file_registry.resolve(missing_reference);
  if (missing.status != IccProfileAssetStatus::AssetNotFound || missing.ready()) {
    std::filesystem::remove_all(test_root);
    return fail("a missing profile asset must not silently fall back to another color space");
  }

  const auto durable_source = test_root / "durable-source.icc";
  const auto durable_store = test_root / "profile-store";
  {
    std::ofstream stream(durable_source, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(payload.data()),
        static_cast<std::streamsize>(payload.size()));
  }
  std::array<std::future<bool>, 8> concurrent_imports;
  for (auto& future : concurrent_imports) {
    future = std::async(
        std::launch::async,
        [durable_store, durable_source, payload_hash]() {
          IccProfileAssetRegistry concurrent_registry(durable_store.string());
          const auto result = concurrent_registry.import_file(
              durable_source.string(),
              "Concurrent Durable Test RGB");
          return result.ready() &&
                 result.reference.payload_sha256 == payload_hash &&
                 std::filesystem::exists(result.resolved_uri);
        });
  }
  for (auto& future : concurrent_imports) {
    if (!future.get()) {
      std::filesystem::remove_all(test_root);
      return fail("concurrent imports should atomically converge on one verified durable payload");
    }
  }
  IccProfileAssetRegistry durable_registry(durable_store.string());
  const auto durable_import = durable_registry.import_file(
      durable_source.string(),
      "Durable Test RGB");
  const auto durable_path =
      durable_registry.stored_path_for_reference(durable_import.reference);
  if (!durable_import.ready() || durable_path.empty() ||
      durable_import.resolved_uri != durable_path ||
      !std::filesystem::exists(durable_path)) {
    std::filesystem::remove_all(test_root);
    return fail("profile import should atomically persist a content-addressed payload");
  }

  std::filesystem::remove(durable_source);
  IccProfileAssetRegistry reopened_registry(durable_store.string());
  const auto reopened = reopened_registry.resolve(durable_import.reference);
  if (!reopened.ready() || reopened.resolved_uri != durable_path ||
      reopened.payload_bytes().size() != payload.size()) {
    std::filesystem::remove_all(test_root);
    return fail("a new process registry should resolve the verified durable copy without the source file");
  }

  const auto relocated_source = test_root / "relocated-profile.icc";
  {
    std::ofstream stream(relocated_source, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(payload.data()),
        static_cast<std::streamsize>(payload.size()));
  }
  IccProfileAssetRegistry relink_registry(durable_store.string());
  relink_registry.register_path(
      durable_import.reference.asset_id,
      relocated_source.string());
  const auto relocated = relink_registry.resolve(durable_import.reference);
  if (!relocated.ready() || relocated.resolved_uri != relocated_source.string()) {
    std::filesystem::remove_all(test_root);
    return fail("an explicit relink must verify the selected path before using the durable copy");
  }
  {
    std::ofstream stream(relocated_source, std::ios::binary | std::ios::trunc);
    stream << "changed-relink-payload";
  }
  relink_registry.register_path(
      durable_import.reference.asset_id,
      relocated_source.string());
  const auto changed_relink = relink_registry.resolve(durable_import.reference);
  if (changed_relink.status != IccProfileAssetStatus::HashMismatch ||
      changed_relink.ready()) {
    std::filesystem::remove_all(test_root);
    return fail("a changed relink target must fail even when a valid durable copy exists");
  }

  {
    std::ofstream stream(durable_path, std::ios::binary | std::ios::trunc);
    stream << "tampered-durable-payload";
  }
  IccProfileAssetRegistry tampered_registry(durable_store.string());
  const auto tampered = tampered_registry.resolve(durable_import.reference);
  if (tampered.status != IccProfileAssetStatus::HashMismatch || tampered.ready()) {
    std::filesystem::remove_all(test_root);
    return fail("a tampered durable payload must fail verification rather than fall back to the source");
  }

  const auto invalid_storage_root = test_root / "not-a-directory";
  {
    std::ofstream stream(invalid_storage_root, std::ios::binary | std::ios::trunc);
    stream << "file-blocking-storage-directory";
  }
  IccProfileAssetRegistry blocked_registry(invalid_storage_root.string());
  const auto blocked = blocked_registry.register_payload(reference, payload);
  if (blocked.status != IccProfileAssetStatus::StorageWriteFailed || blocked.ready() ||
      blocked.failure_reason.empty()) {
    std::filesystem::remove_all(test_root);
    return fail("a configured but unwritable profile store must fail explicitly");
  }

  if (std::string(photo_editor::pipeline::icc_profile_asset_status_name(
          IccProfileAssetStatus::HashMismatch)) != "hash_mismatch") {
    std::filesystem::remove_all(test_root);
    return fail("profile asset statuses should expose stable diagnostic names");
  }
  if (std::string(photo_editor::pipeline::icc_profile_asset_status_name(
          IccProfileAssetStatus::StorageWriteFailed)) != "storage_write_failed") {
    std::filesystem::remove_all(test_root);
    return fail("durable storage failures should expose a stable diagnostic name");
  }
  std::filesystem::remove_all(test_root);
  return EXIT_SUCCESS;
}
