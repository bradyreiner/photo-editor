#include "photo_editor/pipeline/color_profile_asset.h"

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
             value.cbegin(),
             value.cend(),
             [](unsigned char character) {
               return std::isxdigit(character) != 0;
             });
}

[[nodiscard]] ResolvedIccProfileAsset failed_resolution(
    const IccProfileAssetReference& reference,
    IccProfileAssetStatus status,
    std::string reason,
    std::string resolved_uri = {}) {
  return ResolvedIccProfileAsset{
      .reference = reference,
      .status = status,
      .payload = {},
      .resolved_uri = std::move(resolved_uri),
      .failure_reason = std::move(reason),
  };
}

[[nodiscard]] ResolvedIccProfileAsset read_profile_file(
    const IccProfileAssetReference& reference,
    const std::string& path) {
  if (path.empty()) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::AssetNotFound,
        "icc_profile_asset_location_missing");
  }

  std::error_code error;
  const auto file_size = std::filesystem::file_size(path, error);
  if (error) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::AssetNotFound,
        "icc_profile_asset_not_found",
        path);
  }
  if (file_size == 0U) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::ReadFailed,
        "icc_profile_asset_empty",
        path);
  }
  if (file_size > kMaximumIccProfileBytes) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::PayloadTooLarge,
        "icc_profile_asset_exceeds_16_mib_limit",
        path);
  }
  if (file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::PayloadTooLarge,
        "icc_profile_asset_size_not_addressable",
        path);
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::ReadFailed,
        "icc_profile_asset_open_failed",
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
        IccProfileAssetStatus::ReadFailed,
        "icc_profile_asset_read_failed",
        path);
  }

  auto resolved_reference = reference;
  const auto hash = color_payload_sha256(*payload);
  if (!resolved_reference.payload_sha256.empty() &&
      normalized_sha256(resolved_reference.payload_sha256) != hash) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::HashMismatch,
        "icc_profile_asset_sha256_mismatch",
        path);
  }
  if (resolved_reference.byte_size != 0U &&
      resolved_reference.byte_size != payload->size()) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::SizeMismatch,
        "icc_profile_asset_size_mismatch",
        path);
  }
  resolved_reference.payload_sha256 = hash;
  resolved_reference.byte_size = payload->size();
  return ResolvedIccProfileAsset{
      .reference = std::move(resolved_reference),
      .status = IccProfileAssetStatus::Ready,
      .payload = std::move(payload),
      .resolved_uri = path,
      .failure_reason = {},
  };
}

[[nodiscard]] std::filesystem::path stored_profile_path(
    std::string_view storage_root,
    std::string_view payload_sha256) {
  if (storage_root.empty() || !valid_sha256(payload_sha256)) {
    return {};
  }
  const auto normalized_hash = normalized_sha256(std::string(payload_sha256));
  return std::filesystem::path(storage_root) /
         normalized_hash.substr(0, 2) /
         (normalized_hash + ".icc");
}

struct PersistedProfileResult final {
  IccProfileAssetStatus status = IccProfileAssetStatus::Ready;
  std::string path;
  std::string failure_reason;

  [[nodiscard]] bool ready() const {
    return status == IccProfileAssetStatus::Ready;
  }
};

[[nodiscard]] PersistedProfileResult persist_profile_payload(
    const IccProfileAssetReference& reference,
    std::span<const std::byte> payload,
    std::string_view storage_root) {
  const auto destination = stored_profile_path(storage_root, reference.payload_sha256);
  if (destination.empty()) {
    return {
        .status = IccProfileAssetStatus::StorageWriteFailed,
        .failure_reason = "icc_profile_asset_storage_root_invalid",
    };
  }

  std::error_code error;
  if (std::filesystem::exists(destination, error) && !error) {
    const auto existing = read_profile_file(reference, destination.string());
    return existing.ready()
               ? PersistedProfileResult{
                     .status = IccProfileAssetStatus::Ready,
                     .path = destination.string(),
                 }
               : PersistedProfileResult{
                     .status = existing.status,
                     .path = destination.string(),
                     .failure_reason = existing.failure_reason,
                 };
  }
  error.clear();
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    return {
        .status = IccProfileAssetStatus::StorageWriteFailed,
        .path = destination.string(),
        .failure_reason = "icc_profile_asset_storage_directory_create_failed",
    };
  }

  static std::atomic<std::uint64_t> temporary_counter{0};
  const auto temporary_path = destination.string() + ".tmp-" +
                              std::to_string(temporary_counter.fetch_add(1));
  {
    std::ofstream stream(
        temporary_path,
        std::ios::binary | std::ios::trunc);
    if (!stream) {
      return {
          .status = IccProfileAssetStatus::StorageWriteFailed,
          .path = destination.string(),
          .failure_reason = "icc_profile_asset_storage_open_failed",
      };
    }
    stream.write(
        reinterpret_cast<const char*>(payload.data()),
        static_cast<std::streamsize>(payload.size()));
    stream.flush();
    if (!stream) {
      stream.close();
      std::filesystem::remove(temporary_path, error);
      return {
          .status = IccProfileAssetStatus::StorageWriteFailed,
          .path = destination.string(),
          .failure_reason = "icc_profile_asset_storage_write_failed",
      };
    }
  }

  std::filesystem::rename(temporary_path, destination, error);
  if (error) {
    std::error_code remove_error;
    std::filesystem::remove(temporary_path, remove_error);
    const auto concurrent = read_profile_file(reference, destination.string());
    if (concurrent.ready()) {
      return {
          .status = IccProfileAssetStatus::Ready,
          .path = destination.string(),
      };
    }
    return {
        .status = IccProfileAssetStatus::StorageWriteFailed,
        .path = destination.string(),
        .failure_reason = "icc_profile_asset_storage_commit_failed",
    };
  }
  return {
      .status = IccProfileAssetStatus::Ready,
      .path = destination.string(),
  };
}

}  // namespace

bool IccProfileAssetReference::configured() const {
  return !asset_id.empty() || !payload_sha256.empty() || !source_uri.empty();
}

bool IccProfileAssetReference::has_valid_identity() const {
  return !asset_id.empty() && asset_id.size() <= 512U &&
         valid_sha256(payload_sha256) && byte_size > 0U &&
         byte_size <= kMaximumIccProfileBytes;
}

bool ResolvedIccProfileAsset::ready() const {
  return status == IccProfileAssetStatus::Ready && payload != nullptr && !payload->empty();
}

std::span<const std::byte> ResolvedIccProfileAsset::payload_bytes() const {
  return payload != nullptr ? std::span<const std::byte>(*payload) : std::span<const std::byte>{};
}

IccProfileAssetRegistry::IccProfileAssetRegistry(std::string storage_root) {
  set_storage_root(std::move(storage_root));
}

ResolvedIccProfileAsset IccProfileAssetRegistry::import_file(
    std::string path,
    std::string display_name,
    std::string source_kind,
    std::string preferred_asset_id) {
  IccProfileAssetReference provisional{
      .asset_id = preferred_asset_id.empty() ? "pending-import" : preferred_asset_id,
      .payload_sha256 = {},
      .display_name = std::move(display_name),
      .source_uri = path,
      .source_kind = std::move(source_kind),
      .byte_size = 0,
  };
  auto resolved = read_profile_file(provisional, path);
  if (!resolved.ready()) {
    return resolved;
  }
  if (resolved.reference.display_name.empty()) {
    resolved.reference.display_name = std::filesystem::path(path).filename().string();
  }
  if (preferred_asset_id.empty()) {
    resolved.reference.asset_id = "icc:" + resolved.reference.payload_sha256;
  }
  resolved.reference.source_uri = path;
  return register_payload(
      resolved.reference,
      resolved.payload_bytes(),
      std::move(path));
}

ResolvedIccProfileAsset IccProfileAssetRegistry::register_payload(
    const IccProfileAssetReference& reference,
    std::span<const std::byte> payload,
    std::string resolved_uri) {
  if (!reference.configured() || !reference.has_valid_identity()) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::InvalidReference,
        "icc_profile_asset_reference_invalid",
        std::move(resolved_uri));
  }
  if (payload.empty()) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::ReadFailed,
        "icc_profile_asset_empty",
        std::move(resolved_uri));
  }
  if (payload.size() > kMaximumIccProfileBytes) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::PayloadTooLarge,
        "icc_profile_asset_exceeds_16_mib_limit",
        std::move(resolved_uri));
  }
  const auto hash = color_payload_sha256(payload);
  if (normalized_sha256(reference.payload_sha256) != hash) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::HashMismatch,
        "icc_profile_asset_sha256_mismatch",
        std::move(resolved_uri));
  }
  if (reference.byte_size != payload.size()) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::SizeMismatch,
        "icc_profile_asset_size_mismatch",
        std::move(resolved_uri));
  }

  auto immutable_payload =
      std::make_shared<const std::vector<std::byte>>(payload.begin(), payload.end());
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
    resolved_uris_[reference.asset_id] = resolved_uri;
  }
  return ResolvedIccProfileAsset{
      .reference = reference,
      .status = IccProfileAssetStatus::Ready,
      .payload = std::move(immutable_payload),
      .resolved_uri = std::move(resolved_uri),
      .failure_reason = {},
  };
}

void IccProfileAssetRegistry::set_storage_root(std::string storage_root) {
  const auto normalized = storage_root.empty()
                              ? std::string{}
                              : std::filesystem::path(storage_root)
                                    .lexically_normal()
                                    .string();
  const std::lock_guard lock(mutex_);
  if (storage_root_ == normalized) {
    return;
  }
  storage_root_ = normalized;
  payloads_.clear();
  resolved_uris_.clear();
}

std::string IccProfileAssetRegistry::storage_root() const {
  const std::lock_guard lock(mutex_);
  return storage_root_;
}

std::string IccProfileAssetRegistry::stored_path_for_reference(
    const IccProfileAssetReference& reference) const {
  const std::lock_guard lock(mutex_);
  return stored_profile_path(storage_root_, reference.payload_sha256).string();
}

void IccProfileAssetRegistry::register_path(std::string asset_id, std::string path) {
  const std::lock_guard lock(mutex_);
  if (asset_id.empty() || path.empty()) {
    return;
  }
  payloads_.erase(asset_id);
  resolved_uris_.erase(asset_id);
  registered_paths_[std::move(asset_id)] = std::move(path);
}

ResolvedIccProfileAsset IccProfileAssetRegistry::resolve(
    const IccProfileAssetReference& reference) const {
  if (!reference.configured()) {
    return ResolvedIccProfileAsset{.reference = reference};
  }
  if (!reference.has_valid_identity()) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::InvalidReference,
        "icc_profile_asset_reference_invalid");
  }

  std::string path;
  std::string persistent_root;
  {
    const std::lock_guard lock(mutex_);
    const auto payload = payloads_.find(reference.asset_id);
    if (payload != payloads_.end()) {
      const auto hash = color_payload_sha256(*payload->second);
      if (hash != normalized_sha256(reference.payload_sha256)) {
        return failed_resolution(
            reference,
            IccProfileAssetStatus::HashMismatch,
            "icc_profile_asset_cached_sha256_mismatch");
      }
      if (payload->second->size() != reference.byte_size) {
        return failed_resolution(
            reference,
            IccProfileAssetStatus::SizeMismatch,
            "icc_profile_asset_cached_size_mismatch");
      }
      const auto uri = resolved_uris_.find(reference.asset_id);
      return ResolvedIccProfileAsset{
          .reference = reference,
          .status = IccProfileAssetStatus::Ready,
          .payload = payload->second,
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
    auto resolved = read_profile_file(reference, path);
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
      resolved_uris_[reference.asset_id] = path;
    }
    return resolved;
  }

  const auto persistent_path = stored_profile_path(
      persistent_root,
      reference.payload_sha256);
  if (!persistent_path.empty()) {
    std::error_code exists_error;
    const auto stored_exists = std::filesystem::exists(persistent_path, exists_error);
    if (exists_error) {
      return failed_resolution(
          reference,
          IccProfileAssetStatus::ReadFailed,
          "icc_profile_asset_storage_stat_failed",
          persistent_path.string());
    }
    if (stored_exists) {
      auto resolved = read_profile_file(reference, persistent_path.string());
      if (!resolved.ready()) {
        return resolved;
      }
      {
        const std::lock_guard lock(mutex_);
        payloads_[reference.asset_id] = resolved.payload;
        resolved_uris_[reference.asset_id] = persistent_path.string();
      }
      return resolved;
    }
  }

  path = reference.source_uri;
  if (path.empty()) {
    return failed_resolution(
        reference,
        IccProfileAssetStatus::AssetNotFound,
        "icc_profile_asset_location_missing");
  }

  auto resolved = read_profile_file(reference, path);
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
    resolved_uris_[reference.asset_id] = resolved.resolved_uri;
  }
  return resolved;
}

void IccProfileAssetRegistry::clear() {
  const std::lock_guard lock(mutex_);
  payloads_.clear();
  resolved_uris_.clear();
  registered_paths_.clear();
}

const char* icc_profile_asset_status_name(IccProfileAssetStatus status) {
  switch (status) {
    case IccProfileAssetStatus::Unconfigured:
      return "unconfigured";
    case IccProfileAssetStatus::Ready:
      return "ready";
    case IccProfileAssetStatus::InvalidReference:
      return "invalid_reference";
    case IccProfileAssetStatus::AssetNotFound:
      return "asset_not_found";
    case IccProfileAssetStatus::ReadFailed:
      return "read_failed";
    case IccProfileAssetStatus::PayloadTooLarge:
      return "payload_too_large";
    case IccProfileAssetStatus::HashMismatch:
      return "hash_mismatch";
    case IccProfileAssetStatus::SizeMismatch:
      return "size_mismatch";
    case IccProfileAssetStatus::StorageWriteFailed:
      return "storage_write_failed";
  }
  return "unknown";
}

IccProfileAssetRegistry& default_icc_profile_asset_registry() {
  static IccProfileAssetRegistry registry;
  return registry;
}

}  // namespace photo_editor::pipeline
