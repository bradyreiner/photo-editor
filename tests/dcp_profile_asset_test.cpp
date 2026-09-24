#include "photo_editor/pipeline/dcp_profile_asset.h"
#include "dcp_test_fixture.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

bool write_payload(const std::filesystem::path& path, const std::vector<std::byte>& payload) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(
      reinterpret_cast<const char*>(payload.data()),
      static_cast<std::streamsize>(payload.size()));
  return static_cast<bool>(stream);
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

class TemporaryTree final {
 public:
  TemporaryTree() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("photo-editor-dcp-assets-" + std::to_string(suffix));
    std::filesystem::create_directories(path_);
  }

  ~TemporaryTree() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

int main() {
  using photo_editor::pipeline::DcpProfileAssetRegistry;
  using photo_editor::pipeline::DcpProfileAssetStatus;

  TemporaryTree tree;
  const auto source = tree.path() / "source.dcp";
  const auto store = tree.path() / "store";
  const auto payload = photo_editor::test::valid_dcp_asset_payload();
  if (!write_payload(source, payload)) {
    return fail("DCP fixture should be writable");
  }

  DcpProfileAssetRegistry registry(store.string());
  const auto imported = registry.import_file(source.string());
  if (!imported.ready() || imported.reference.schema_version != 1U ||
      imported.reference.asset_id.rfind("dcp:", 0U) != 0U ||
      imported.reference.payload_sha256.size() != 64U ||
      imported.reference.byte_size != payload.size() ||
      imported.profile->profile_name != "Asset Fixture" ||
      imported.resolved_uri != registry.stored_path_for_reference(imported.reference)) {
    return fail("DCP import should parse and persist one immutable content identity");
  }
  const auto durable_path = registry.stored_path_for_reference(imported.reference);
  if (std::filesystem::path(durable_path).extension() != ".dcp" ||
      !std::filesystem::exists(durable_path)) {
    return fail("durable DCP payload should use a content-addressed .dcp path");
  }

  std::filesystem::remove(source);
  DcpProfileAssetRegistry restarted(store.string());
  const auto after_restart = restarted.resolve(imported.reference);
  if (!after_restart.ready() || after_restart.resolved_uri != durable_path ||
      after_restart.profile->payload_sha256 != imported.reference.payload_sha256) {
    return fail("DCP assets should resolve from durable storage after source deletion and restart");
  }

  const auto relink_source = tree.path() / "relinked.dcp";
  if (!write_payload(relink_source, payload)) {
    return fail("relink fixture should be writable");
  }
  DcpProfileAssetRegistry relinked(store.string());
  relinked.register_path(imported.reference.asset_id, relink_source.string());
  const auto relink_result = relinked.resolve(imported.reference);
  if (!relink_result.ready() || relink_result.resolved_uri != relink_source.string()) {
    return fail("explicit DCP relink should accept only the same payload identity");
  }

  auto changed_payload = payload;
  changed_payload.back() ^= std::byte{1};
  if (!write_payload(relink_source, changed_payload)) {
    return fail("changed relink fixture should be writable");
  }
  DcpProfileAssetRegistry changed_relink(store.string());
  changed_relink.register_path(imported.reference.asset_id, relink_source.string());
  const auto changed_result = changed_relink.resolve(imported.reference);
  if (changed_result.status != DcpProfileAssetStatus::HashMismatch) {
    return fail("changed DCP relink bytes should be rejected even when a durable copy exists");
  }

  auto durable_payload = payload;
  durable_payload[0] = std::byte{'M'};
  if (!write_payload(durable_path, durable_payload)) {
    return fail("tampered durable fixture should be writable");
  }
  DcpProfileAssetRegistry tampered(store.string());
  const auto tampered_result = tampered.resolve(imported.reference);
  if (tampered_result.status != DcpProfileAssetStatus::HashMismatch) {
    return fail("tampered durable DCP bytes should fail without falling back to a source path");
  }

  TemporaryTree invalid_tree;
  const auto invalid_source = invalid_tree.path() / "invalid.dcp";
  const std::vector<std::byte> invalid_payload = {
      std::byte{'I'}, std::byte{'I'}, std::byte{0x52}, std::byte{0x43},
      std::byte{8}, std::byte{0}, std::byte{0}, std::byte{0},
  };
  if (!write_payload(invalid_source, invalid_payload)) {
    return fail("invalid DCP fixture should be writable");
  }
  DcpProfileAssetRegistry invalid_registry((invalid_tree.path() / "store").string());
  const auto invalid = invalid_registry.import_file(invalid_source.string());
  if (invalid.status != DcpProfileAssetStatus::ParseFailed ||
      std::filesystem::exists(invalid_tree.path() / "store")) {
    return fail("structurally invalid DCP bytes must be rejected before persistence");
  }

  auto invalid_reference = imported.reference;
  invalid_reference.schema_version = 99U;
  if (registry.resolve(invalid_reference).status !=
      DcpProfileAssetStatus::InvalidReference) {
    return fail("unknown DCP asset schema versions should fail closed");
  }

  TemporaryTree concurrent_tree;
  const auto concurrent_source = concurrent_tree.path() / "source.dcp";
  if (!write_payload(concurrent_source, payload)) {
    return fail("concurrent DCP fixture should be writable");
  }
  DcpProfileAssetRegistry concurrent_registry((concurrent_tree.path() / "store").string());
  std::array<photo_editor::pipeline::ResolvedDcpProfileAsset, 4> concurrent_results;
  std::array<std::thread, 4> workers;
  for (std::size_t index = 0U; index < workers.size(); ++index) {
    workers[index] = std::thread([&, index]() {
      concurrent_results[index] = concurrent_registry.import_file(concurrent_source.string());
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  for (const auto& result : concurrent_results) {
    if (!result.ready() ||
        result.reference.payload_sha256 != concurrent_results.front().reference.payload_sha256 ||
        result.resolved_uri != concurrent_results.front().resolved_uri) {
      return fail("concurrent DCP imports should converge on one verified durable payload");
    }
  }

  if (std::string(photo_editor::pipeline::dcp_profile_asset_status_name(
          DcpProfileAssetStatus::Ready)) != "ready" ||
      std::string(photo_editor::pipeline::dcp_profile_asset_status_name(
          DcpProfileAssetStatus::ParseFailed)) != "parse_failed") {
    return fail("DCP asset status names should remain stable for diagnostics");
  }

  return 0;
}
