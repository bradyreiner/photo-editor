#pragma once

#include "photo_editor/pipeline/dcp_profile.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace photo_editor::pipeline {

inline constexpr std::uint32_t kDcpProfileAssetSchemaVersion = 1U;

struct DcpProfileAssetReference final {
  std::uint32_t schema_version = kDcpProfileAssetSchemaVersion;
  std::string asset_id;
  std::string payload_sha256;
  std::string display_name;
  std::string source_uri;
  std::string source_kind;
  std::uint64_t byte_size = 0U;

  [[nodiscard]] bool configured() const;
  [[nodiscard]] bool has_valid_identity() const;
};

enum class DcpProfileAssetStatus {
  Unconfigured,
  Ready,
  InvalidReference,
  AssetNotFound,
  ReadFailed,
  PayloadTooLarge,
  HashMismatch,
  SizeMismatch,
  StorageWriteFailed,
  ParseFailed,
};

struct ResolvedDcpProfileAsset final {
  DcpProfileAssetReference reference;
  DcpProfileAssetStatus status = DcpProfileAssetStatus::Unconfigured;
  std::shared_ptr<const std::vector<std::byte>> payload;
  std::shared_ptr<const DcpProfile> profile;
  std::string resolved_uri;
  std::string failure_reason;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] std::span<const std::byte> payload_bytes() const;
};

class DcpProfileAssetRegistry final {
 public:
  DcpProfileAssetRegistry() = default;
  explicit DcpProfileAssetRegistry(std::string storage_root);

  [[nodiscard]] ResolvedDcpProfileAsset import_file(
      std::string path,
      std::string display_name = {},
      std::string source_kind = "user-file",
      std::string preferred_asset_id = {});
  [[nodiscard]] ResolvedDcpProfileAsset register_payload(
      const DcpProfileAssetReference& reference,
      std::span<const std::byte> payload,
      std::string resolved_uri = {});
  void register_path(std::string asset_id, std::string path);
  void set_storage_root(std::string storage_root);
  [[nodiscard]] std::string storage_root() const;
  [[nodiscard]] std::string stored_path_for_reference(
      const DcpProfileAssetReference& reference) const;
  [[nodiscard]] ResolvedDcpProfileAsset resolve(
      const DcpProfileAssetReference& reference) const;
  void clear();

 private:
  mutable std::mutex mutex_;
  mutable std::unordered_map<std::string, std::shared_ptr<const std::vector<std::byte>>> payloads_;
  mutable std::unordered_map<std::string, std::shared_ptr<const DcpProfile>> profiles_;
  mutable std::unordered_map<std::string, std::string> resolved_uris_;
  std::unordered_map<std::string, std::string> registered_paths_;
  std::string storage_root_;
};

[[nodiscard]] const char* dcp_profile_asset_status_name(DcpProfileAssetStatus status);
[[nodiscard]] DcpProfileAssetRegistry& default_dcp_profile_asset_registry();

}  // namespace photo_editor::pipeline
