#pragma once

#include "photo_editor/pipeline/color_transform.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace photo_editor::pipeline {

struct IccProfileAssetReference final {
  std::string asset_id;
  std::string payload_sha256;
  std::string display_name;
  std::string source_uri;
  std::string source_kind;
  std::uint64_t byte_size = 0;

  [[nodiscard]] bool configured() const;
  [[nodiscard]] bool has_valid_identity() const;
};

enum class IccProfileAssetStatus {
  Unconfigured,
  Ready,
  InvalidReference,
  AssetNotFound,
  ReadFailed,
  PayloadTooLarge,
  HashMismatch,
  SizeMismatch,
  StorageWriteFailed,
};

struct ResolvedIccProfileAsset final {
  IccProfileAssetReference reference;
  IccProfileAssetStatus status = IccProfileAssetStatus::Unconfigured;
  std::shared_ptr<const std::vector<std::byte>> payload;
  std::string resolved_uri;
  std::string failure_reason;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] std::span<const std::byte> payload_bytes() const;
};

class IccProfileAssetRegistry final {
 public:
  IccProfileAssetRegistry() = default;
  explicit IccProfileAssetRegistry(std::string storage_root);

  [[nodiscard]] ResolvedIccProfileAsset import_file(
      std::string path,
      std::string display_name = {},
      std::string source_kind = "user-file",
      std::string preferred_asset_id = {});
  [[nodiscard]] ResolvedIccProfileAsset register_payload(
      const IccProfileAssetReference& reference,
      std::span<const std::byte> payload,
      std::string resolved_uri = {});
  void register_path(std::string asset_id, std::string path);
  void set_storage_root(std::string storage_root);
  [[nodiscard]] std::string storage_root() const;
  [[nodiscard]] std::string stored_path_for_reference(
      const IccProfileAssetReference& reference) const;
  [[nodiscard]] ResolvedIccProfileAsset resolve(
      const IccProfileAssetReference& reference) const;
  void clear();

 private:
  mutable std::mutex mutex_;
  mutable std::unordered_map<
      std::string,
      std::shared_ptr<const std::vector<std::byte>>> payloads_;
  mutable std::unordered_map<std::string, std::string> resolved_uris_;
  std::unordered_map<std::string, std::string> registered_paths_;
  std::string storage_root_;
};

[[nodiscard]] const char* icc_profile_asset_status_name(IccProfileAssetStatus status);
[[nodiscard]] IccProfileAssetRegistry& default_icc_profile_asset_registry();

}  // namespace photo_editor::pipeline
