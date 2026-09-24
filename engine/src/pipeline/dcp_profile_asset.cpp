#include "photo_editor/pipeline/dcp_profile_asset.h"

#include "photo_editor/pipeline/color_transform.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace photo_editor::pipeline {
namespace {

[[nodiscard]] std::string normalized_sha256(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  return value;
}

[[nodiscard]] bool valid_sha256(std::string_view value) {
  return value.size() == 64U &&
         std::all_of(
             value.begin(),
             value.end(),
             [](unsigned char character) {
               return std::isxdigit(character) != 0;
             });
}

[[nodiscard]] ResolvedDcpProfileAsset failed_resolution(
    const DcpProfileAssetReference& reference,
    DcpProfileAssetStatus status,
    std::string reason,
    std::string resolved_uri = {}) {
  return ResolvedDcpProfileAsset{
      .reference = reference,
      .status = status,
      .payload = {},
      .profile = {},
      .resolved_uri = std::move(resolved_uri),
      .failure_reason = std::move(reason),
  };
}

[[nodiscard]] std::filesystem::path stored_profile_path(
    std::string_view storage_root,
    std::string_view payload_sha256) {
  if (storage_root.empty() || !valid_sha256(payload_sha256)) {
    return {};
  }
  const auto hash = normalized_sha256(std::string(payload_sha256));
  return std::filesystem::path(storage_root) /
         hash.substr(0U, 2U) /
         (hash + ".dcp");
}

[[nodiscard]] ResolvedDcpProfileAsset read_and_parse_profile_file(
    const DcpProfileAssetReference& reference,
    const std::string& path) {
  if (path.empty()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::AssetNotFound,
        "dcp_profile_asset_location_missing");
  }
  std::error_code error;
  const auto file_size = std::filesystem::file_size(path, error);
  if (error) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::AssetNotFound,
        "dcp_profile_asset_not_found",
        path);
  }
  if (file_size == 0U) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::ReadFailed,
        "dcp_profile_asset_empty",
        path);
  }
  if (file_size > kMaximumDcpProfileBytes ||
      file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::PayloadTooLarge,
        "dcp_profile_asset_exceeds_64_mib_limit",
        path);
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::ReadFailed,
        "dcp_profile_asset_open_failed",
        path);
  }
  auto payload = std::make_shared<std::vector<std::byte>>(
      static_cast<std::size_t>(file_size));
  stream.read(
      reinterpret_cast<char*>(payload->data()),
      static_cast<std::streamsize>(payload->size()));
  if (!stream || static_cast<std::size_t>(stream.gcount()) != payload->size()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::ReadFailed,
        "dcp_profile_asset_read_failed",
        path);
  }

  auto resolved_reference = reference;
  const auto hash = color_payload_sha256(*payload);
  if (!resolved_reference.payload_sha256.empty() &&
      normalized_sha256(resolved_reference.payload_sha256) != hash) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::HashMismatch,
        "dcp_profile_asset_sha256_mismatch",
        path);
  }
  if (resolved_reference.byte_size != 0U &&
      resolved_reference.byte_size != payload->size()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::SizeMismatch,
        "dcp_profile_asset_size_mismatch",
        path);
  }
  const auto parsed = parse_dcp_profile(*payload);
  if (!parsed.ready()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::ParseFailed,
        parsed.failure_reason.empty() ? "dcp_profile_asset_parse_failed"
                                      : parsed.failure_reason,
        path);
  }
  resolved_reference.payload_sha256 = hash;
  resolved_reference.byte_size = payload->size();
  return ResolvedDcpProfileAsset{
      .reference = std::move(resolved_reference),
      .status = DcpProfileAssetStatus::Ready,
      .payload = std::move(payload),
      .profile = std::make_shared<const DcpProfile>(parsed.profile),
      .resolved_uri = path,
      .failure_reason = {},
  };
}

struct PersistResult final {
  DcpProfileAssetStatus status = DcpProfileAssetStatus::Ready;
  std::string path;
  std::string failure_reason;

  [[nodiscard]] bool ready() const {
    return status == DcpProfileAssetStatus::Ready;
  }
};

[[nodiscard]] PersistResult persist_profile_payload(
    const DcpProfileAssetReference& reference,
    std::span<const std::byte> payload,
    std::string_view storage_root) {
  const auto destination = stored_profile_path(storage_root, reference.payload_sha256);
  if (destination.empty()) {
    return {
        .status = DcpProfileAssetStatus::StorageWriteFailed,
        .failure_reason = "dcp_profile_asset_storage_root_invalid",
    };
  }
  std::error_code error;
  if (std::filesystem::exists(destination, error) && !error) {
    const auto existing = read_and_parse_profile_file(reference, destination.string());
    return existing.ready()
               ? PersistResult{
                     .status = DcpProfileAssetStatus::Ready,
                     .path = destination.string(),
                 }
               : PersistResult{
                     .status = existing.status,
                     .path = destination.string(),
                     .failure_reason = existing.failure_reason,
                 };
  }
  error.clear();
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    return {
        .status = DcpProfileAssetStatus::StorageWriteFailed,
        .path = destination.string(),
        .failure_reason = "dcp_profile_asset_storage_directory_create_failed",
    };
  }
  static std::atomic<std::uint64_t> counter{0U};
  const auto temporary = destination.string() + ".tmp-" +
                         std::to_string(counter.fetch_add(1U));
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return {
          .status = DcpProfileAssetStatus::StorageWriteFailed,
          .path = destination.string(),
          .failure_reason = "dcp_profile_asset_storage_open_failed",
      };
    }
    stream.write(
        reinterpret_cast<const char*>(payload.data()),
        static_cast<std::streamsize>(payload.size()));
    stream.flush();
    if (!stream) {
      stream.close();
      std::filesystem::remove(temporary, error);
      return {
          .status = DcpProfileAssetStatus::StorageWriteFailed,
          .path = destination.string(),
          .failure_reason = "dcp_profile_asset_storage_write_failed",
      };
    }
  }
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    const auto concurrent = read_and_parse_profile_file(reference, destination.string());
    if (concurrent.ready()) {
      return {
          .status = DcpProfileAssetStatus::Ready,
          .path = destination.string(),
      };
    }
    return {
        .status = DcpProfileAssetStatus::StorageWriteFailed,
        .path = destination.string(),
        .failure_reason = "dcp_profile_asset_storage_commit_failed",
    };
  }
  return {
      .status = DcpProfileAssetStatus::Ready,
      .path = destination.string(),
  };
}

}  // namespace

bool DcpProfileAssetReference::configured() const {
  return !asset_id.empty() || !payload_sha256.empty() || !source_uri.empty();
}

bool DcpProfileAssetReference::has_valid_identity() const {
  return schema_version == kDcpProfileAssetSchemaVersion &&
         !asset_id.empty() && asset_id.size() <= 512U &&
         valid_sha256(payload_sha256) && byte_size > 0U &&
         byte_size <= kMaximumDcpProfileBytes;
}

bool ResolvedDcpProfileAsset::ready() const {
  return status == DcpProfileAssetStatus::Ready && payload != nullptr &&
         !payload->empty() && profile != nullptr;
}

std::span<const std::byte> ResolvedDcpProfileAsset::payload_bytes() const {
  return payload != nullptr ? std::span<const std::byte>(*payload)
                            : std::span<const std::byte>{};
}

DcpProfileAssetRegistry::DcpProfileAssetRegistry(std::string storage_root) {
  set_storage_root(std::move(storage_root));
}

ResolvedDcpProfileAsset DcpProfileAssetRegistry::import_file(
    std::string path,
    std::string display_name,
    std::string source_kind,
    std::string preferred_asset_id) {
  DcpProfileAssetReference provisional{
      .schema_version = kDcpProfileAssetSchemaVersion,
      .asset_id = preferred_asset_id.empty() ? "pending-import" : preferred_asset_id,
      .payload_sha256 = {},
      .display_name = std::move(display_name),
      .source_uri = path,
      .source_kind = std::move(source_kind),
      .byte_size = 0U,
  };
  auto resolved = read_and_parse_profile_file(provisional, path);
  if (!resolved.ready()) {
    return resolved;
  }
  if (resolved.reference.display_name.empty()) {
    resolved.reference.display_name = resolved.profile->profile_name.empty()
                                          ? std::filesystem::path(path).filename().string()
                                          : resolved.profile->profile_name;
  }
  if (preferred_asset_id.empty()) {
    resolved.reference.asset_id = "dcp:" + resolved.reference.payload_sha256;
  }
  resolved.reference.source_uri = path;
  return register_payload(
      resolved.reference,
      resolved.payload_bytes(),
      std::move(path));
}

ResolvedDcpProfileAsset DcpProfileAssetRegistry::register_payload(
    const DcpProfileAssetReference& reference,
    std::span<const std::byte> payload,
    std::string resolved_uri) {
  if (!reference.configured() || !reference.has_valid_identity()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::InvalidReference,
        "dcp_profile_asset_reference_invalid",
        std::move(resolved_uri));
  }
  if (payload.empty()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::ReadFailed,
        "dcp_profile_asset_empty",
        std::move(resolved_uri));
  }
  if (payload.size() > kMaximumDcpProfileBytes) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::PayloadTooLarge,
        "dcp_profile_asset_exceeds_64_mib_limit",
        std::move(resolved_uri));
  }
  const auto hash = color_payload_sha256(payload);
  if (normalized_sha256(reference.payload_sha256) != hash) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::HashMismatch,
        "dcp_profile_asset_sha256_mismatch",
        std::move(resolved_uri));
  }
  if (reference.byte_size != payload.size()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::SizeMismatch,
        "dcp_profile_asset_size_mismatch",
        std::move(resolved_uri));
  }
  const auto parsed = parse_dcp_profile(payload);
  if (!parsed.ready()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::ParseFailed,
        parsed.failure_reason.empty() ? "dcp_profile_asset_parse_failed"
                                      : parsed.failure_reason,
        std::move(resolved_uri));
  }

  auto immutable_payload =
      std::make_shared<const std::vector<std::byte>>(payload.begin(), payload.end());
  auto immutable_profile = std::make_shared<const DcpProfile>(parsed.profile);
  std::string persistent_root;
  {
    const std::lock_guard lock(mutex_);
    persistent_root = storage_root_;
  }
  if (!persistent_root.empty()) {
    const auto persisted = persist_profile_payload(reference, payload, persistent_root);
    if (!persisted.ready()) {
      return failed_resolution(
          reference,
          persisted.status,
          persisted.failure_reason,
          persisted.path);
    }
    resolved_uri = persisted.path;
  }
  {
    const std::lock_guard lock(mutex_);
    payloads_[reference.asset_id] = immutable_payload;
    profiles_[reference.asset_id] = immutable_profile;
    resolved_uris_[reference.asset_id] = resolved_uri;
  }
  return ResolvedDcpProfileAsset{
      .reference = reference,
      .status = DcpProfileAssetStatus::Ready,
      .payload = std::move(immutable_payload),
      .profile = std::move(immutable_profile),
      .resolved_uri = std::move(resolved_uri),
      .failure_reason = {},
  };
}

void DcpProfileAssetRegistry::set_storage_root(std::string storage_root) {
  const auto normalized = storage_root.empty()
                              ? std::string{}
                              : std::filesystem::path(storage_root).lexically_normal().string();
  const std::lock_guard lock(mutex_);
  if (storage_root_ == normalized) {
    return;
  }
  storage_root_ = normalized;
  payloads_.clear();
  profiles_.clear();
  resolved_uris_.clear();
}

std::string DcpProfileAssetRegistry::storage_root() const {
  const std::lock_guard lock(mutex_);
  return storage_root_;
}

std::string DcpProfileAssetRegistry::stored_path_for_reference(
    const DcpProfileAssetReference& reference) const {
  const std::lock_guard lock(mutex_);
  return stored_profile_path(storage_root_, reference.payload_sha256).string();
}

void DcpProfileAssetRegistry::register_path(std::string asset_id, std::string path) {
  const std::lock_guard lock(mutex_);
  if (asset_id.empty() || path.empty()) {
    return;
  }
  payloads_.erase(asset_id);
  profiles_.erase(asset_id);
  resolved_uris_.erase(asset_id);
  registered_paths_[std::move(asset_id)] = std::move(path);
}

ResolvedDcpProfileAsset DcpProfileAssetRegistry::resolve(
    const DcpProfileAssetReference& reference) const {
  if (!reference.configured()) {
    return ResolvedDcpProfileAsset{.reference = reference};
  }
  if (!reference.has_valid_identity()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::InvalidReference,
        "dcp_profile_asset_reference_invalid");
  }

  std::string path;
  std::string persistent_root;
  {
    const std::lock_guard lock(mutex_);
    const auto payload = payloads_.find(reference.asset_id);
    const auto profile = profiles_.find(reference.asset_id);
    if (payload != payloads_.end() && profile != profiles_.end()) {
      if (color_payload_sha256(*payload->second) !=
          normalized_sha256(reference.payload_sha256)) {
        return failed_resolution(
            reference,
            DcpProfileAssetStatus::HashMismatch,
            "dcp_profile_asset_cached_sha256_mismatch");
      }
      if (payload->second->size() != reference.byte_size) {
        return failed_resolution(
            reference,
            DcpProfileAssetStatus::SizeMismatch,
            "dcp_profile_asset_cached_size_mismatch");
      }
      const auto uri = resolved_uris_.find(reference.asset_id);
      return ResolvedDcpProfileAsset{
          .reference = reference,
          .status = DcpProfileAssetStatus::Ready,
          .payload = payload->second,
          .profile = profile->second,
          .resolved_uri = uri == resolved_uris_.end() ? std::string{} : uri->second,
          .failure_reason = {},
      };
    }
    const auto registered = registered_paths_.find(reference.asset_id);
    if (registered != registered_paths_.end()) {
      path = registered->second;
    }
    persistent_root = storage_root_;
  }

  if (!path.empty()) {
    auto resolved = read_and_parse_profile_file(reference, path);
    if (!resolved.ready()) {
      return resolved;
    }
    if (!persistent_root.empty()) {
      const auto persisted =
          persist_profile_payload(reference, resolved.payload_bytes(), persistent_root);
      if (!persisted.ready()) {
        return failed_resolution(
            reference,
            persisted.status,
            persisted.failure_reason,
            persisted.path);
      }
    }
    {
      const std::lock_guard lock(mutex_);
      payloads_[reference.asset_id] = resolved.payload;
      profiles_[reference.asset_id] = resolved.profile;
      resolved_uris_[reference.asset_id] = path;
    }
    return resolved;
  }

  const auto persistent_path = stored_profile_path(
      persistent_root,
      reference.payload_sha256);
  if (!persistent_path.empty()) {
    std::error_code exists_error;
    const auto exists = std::filesystem::exists(persistent_path, exists_error);
    if (exists_error) {
      return failed_resolution(
          reference,
          DcpProfileAssetStatus::ReadFailed,
          "dcp_profile_asset_storage_stat_failed",
          persistent_path.string());
    }
    if (exists) {
      auto resolved = read_and_parse_profile_file(reference, persistent_path.string());
      if (!resolved.ready()) {
        return resolved;
      }
      {
        const std::lock_guard lock(mutex_);
        payloads_[reference.asset_id] = resolved.payload;
        profiles_[reference.asset_id] = resolved.profile;
        resolved_uris_[reference.asset_id] = persistent_path.string();
      }
      return resolved;
    }
  }

  path = reference.source_uri;
  if (path.empty()) {
    return failed_resolution(
        reference,
        DcpProfileAssetStatus::AssetNotFound,
        "dcp_profile_asset_location_missing");
  }
  auto resolved = read_and_parse_profile_file(reference, path);
  if (!resolved.ready()) {
    return resolved;
  }
  if (!persistent_root.empty()) {
    const auto persisted =
        persist_profile_payload(reference, resolved.payload_bytes(), persistent_root);
    if (!persisted.ready()) {
      return failed_resolution(
          reference,
          persisted.status,
          persisted.failure_reason,
          persisted.path);
    }
    resolved.resolved_uri = persisted.path;
  }
  {
    const std::lock_guard lock(mutex_);
    payloads_[reference.asset_id] = resolved.payload;
    profiles_[reference.asset_id] = resolved.profile;
    resolved_uris_[reference.asset_id] = resolved.resolved_uri;
  }
  return resolved;
}

void DcpProfileAssetRegistry::clear() {
  const std::lock_guard lock(mutex_);
  payloads_.clear();
  profiles_.clear();
  resolved_uris_.clear();
  registered_paths_.clear();
}

const char* dcp_profile_asset_status_name(DcpProfileAssetStatus status) {
  switch (status) {
    case DcpProfileAssetStatus::Unconfigured:
      return "unconfigured";
    case DcpProfileAssetStatus::Ready:
      return "ready";
    case DcpProfileAssetStatus::InvalidReference:
      return "invalid_reference";
    case DcpProfileAssetStatus::AssetNotFound:
      return "asset_not_found";
    case DcpProfileAssetStatus::ReadFailed:
      return "read_failed";
    case DcpProfileAssetStatus::PayloadTooLarge:
      return "payload_too_large";
    case DcpProfileAssetStatus::HashMismatch:
      return "hash_mismatch";
    case DcpProfileAssetStatus::SizeMismatch:
      return "size_mismatch";
    case DcpProfileAssetStatus::StorageWriteFailed:
      return "storage_write_failed";
    case DcpProfileAssetStatus::ParseFailed:
      return "parse_failed";
  }
  return "unknown";
}

DcpProfileAssetRegistry& default_dcp_profile_asset_registry() {
  static DcpProfileAssetRegistry registry;
  return registry;
}

}  // namespace photo_editor::pipeline
