#include "photo_editor/pipeline/ocio_project_config.h"

#include "photo_editor/pipeline/color_transform.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
#include <OpenColorIO/OpenColorIO.h>
#include <OpenColorIO/OpenColorTransforms.h>
namespace OCIO = OCIO_NAMESPACE;
#endif

namespace photo_editor::pipeline {

struct OcioProjectProcessor::Impl final {
  OcioProjectProcessorDescription description;
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  OCIO::ConstProcessorRcPtr processor;
  OCIO::ConstCPUProcessorRcPtr cpu_processor;
#endif
};

struct OcioProjectGpuProgram::Impl final {
  OcioProjectGpuProgramDescription description;
};

struct OcioProjectConfig::Impl final {
  OcioProjectConfigDescription description;
  mutable std::mutex dependency_mutex;
  mutable std::map<std::string, std::string> dependency_hashes;
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  OCIO::ConstConfigRcPtr config;
  OCIO::ConstContextRcPtr context;
#endif
};

namespace {

#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
constexpr std::size_t kMaximumOcioGpuShaderBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumOcioGpuTextureBytes = 256U * 1024U * 1024U;
constexpr std::size_t kMaximumOcioGpuTextures = 64U;

[[nodiscard]] std::string safe_string(const char* value) {
  return value == nullptr ? std::string{} : std::string(value);
}
#endif

[[nodiscard]] std::string lower_extension(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(
      extension.begin(),
      extension.end(),
      extension.begin(),
      [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  return extension;
}

[[nodiscard]] std::string file_sha256(const std::filesystem::path& path) {
  return color_file_sha256(path);
}

[[nodiscard]] bool source_provenance_unchanged(
    const OcioProjectConfig::Impl& impl) {
  if (impl.description.source_kind == "builtin-uri") {
    return true;
  }
  return !impl.description.source_path.empty() &&
         !impl.description.source_sha256.empty() &&
         file_sha256(impl.description.source_path) ==
             impl.description.source_sha256;
}

[[nodiscard]] bool dependencies_unchanged(
    const OcioProjectConfig::Impl& impl) {
  if (!source_provenance_unchanged(impl)) {
    return false;
  }
  const std::lock_guard lock(impl.dependency_mutex);
  for (const auto& [path, expected_sha256] : impl.dependency_hashes) {
    if (file_sha256(path) != expected_sha256) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool supported_config_source(std::string_view source_path) {
  if (source_path.starts_with("ocio://")) {
    return true;
  }
  const auto extension = lower_extension(std::filesystem::path(source_path));
  return extension == ".ocio" || extension == ".ocioz";
}

[[nodiscard]] std::string config_source_kind(std::string_view source_path) {
  if (source_path.starts_with("ocio://")) {
    return "builtin-uri";
  }
  return lower_extension(std::filesystem::path(source_path)) == ".ocioz"
             ? "ocioz-archive"
             : "ocio-file";
}

[[nodiscard]] OcioProjectProcessor failed_processor(
    const OcioProjectProcessorRequest& request,
    std::string config_cache_id,
    OcioProjectStatus status,
    std::string failure_reason) {
  auto impl = std::make_shared<OcioProjectProcessor::Impl>();
  impl->description.status = status;
  impl->description.kind = request.kind;
  impl->description.config_cache_id = std::move(config_cache_id);
  impl->description.source_color_space = request.source_color_space;
  impl->description.destination_color_space = request.destination_color_space;
  impl->description.display = request.display;
  impl->description.view = request.view;
  impl->description.looks = request.looks;
  impl->description.file_transform_path = request.file_transform_path;
  impl->description.inverse = request.inverse;
  impl->description.failure_reason = std::move(failure_reason);
  return OcioProjectProcessor(std::move(impl));
}

[[nodiscard]] OcioProjectGpuProgram failed_gpu_program(
    const OcioProjectProcessorDescription& processor,
    OcioProjectStatus status,
    std::string failure_reason) {
  auto impl = std::make_shared<OcioProjectGpuProgram::Impl>();
  impl->description.status = status;
  impl->description.config_cache_id = processor.config_cache_id;
  impl->description.processor_cache_id = processor.processor_cache_id;
  impl->description.failure_reason = std::move(failure_reason);
  return OcioProjectGpuProgram(std::move(impl));
}

#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
[[nodiscard]] bool checked_product(
    std::initializer_list<std::size_t> factors,
    std::size_t* result) {
  if (result == nullptr) {
    return false;
  }
  auto value = std::size_t{1U};
  for (const auto factor : factors) {
    if (factor != 0U &&
        value > std::numeric_limits<std::size_t>::max() / factor) {
      return false;
    }
    value *= factor;
  }
  *result = value;
  return true;
}

[[nodiscard]] std::string sha256_string(std::string_view value) {
  return color_payload_sha256(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(value.data()), value.size()));
}
#endif

}  // namespace

const char* ocio_project_status_name(OcioProjectStatus status) {
  switch (status) {
    case OcioProjectStatus::Ready:
      return "ready";
    case OcioProjectStatus::BackendUnavailable:
      return "backend-unavailable";
    case OcioProjectStatus::PathEmpty:
      return "path-empty";
    case OcioProjectStatus::FileUnavailable:
      return "file-unavailable";
    case OcioProjectStatus::UnsupportedSource:
      return "unsupported-source";
    case OcioProjectStatus::ParseFailed:
      return "parse-failed";
    case OcioProjectStatus::ValidationFailed:
      return "validation-failed";
    case OcioProjectStatus::InvalidRequest:
      return "invalid-request";
    case OcioProjectStatus::ProcessorCompileFailed:
      return "processor-compile-failed";
    case OcioProjectStatus::ApplyFailed:
      return "apply-failed";
    case OcioProjectStatus::ArchiveFailed:
      return "archive-failed";
  }
  return "backend-unavailable";
}

OcioProjectProcessor::OcioProjectProcessor()
    : impl_(std::make_shared<Impl>()) {}
OcioProjectProcessor::~OcioProjectProcessor() = default;
OcioProjectProcessor::OcioProjectProcessor(const OcioProjectProcessor&) = default;
OcioProjectProcessor& OcioProjectProcessor::operator=(
    const OcioProjectProcessor&) = default;
OcioProjectProcessor::OcioProjectProcessor(OcioProjectProcessor&&) noexcept = default;
OcioProjectProcessor& OcioProjectProcessor::operator=(
    OcioProjectProcessor&&) noexcept = default;
OcioProjectProcessor::OcioProjectProcessor(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

OcioProjectGpuProgram::OcioProjectGpuProgram()
    : impl_(std::make_shared<Impl>()) {}
OcioProjectGpuProgram::~OcioProjectGpuProgram() = default;
OcioProjectGpuProgram::OcioProjectGpuProgram(
    const OcioProjectGpuProgram&) = default;
OcioProjectGpuProgram& OcioProjectGpuProgram::operator=(
    const OcioProjectGpuProgram&) = default;
OcioProjectGpuProgram::OcioProjectGpuProgram(
    OcioProjectGpuProgram&&) noexcept = default;
OcioProjectGpuProgram& OcioProjectGpuProgram::operator=(
    OcioProjectGpuProgram&&) noexcept = default;
OcioProjectGpuProgram::OcioProjectGpuProgram(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

bool OcioProjectGpuProgram::ready() const {
  return impl_ != nullptr &&
         impl_->description.status == OcioProjectStatus::Ready;
}

const OcioProjectGpuProgramDescription&
OcioProjectGpuProgram::description() const {
  static const OcioProjectGpuProgramDescription unavailable;
  return impl_ == nullptr ? unavailable : impl_->description;
}

bool OcioProjectProcessor::ready() const {
  return impl_ != nullptr &&
         impl_->description.status == OcioProjectStatus::Ready;
}

const OcioProjectProcessorDescription& OcioProjectProcessor::description() const {
  static const OcioProjectProcessorDescription unavailable;
  return impl_ == nullptr ? unavailable : impl_->description;
}

bool OcioProjectProcessor::apply(
    std::span<OcioProjectRgbaFloat> pixels) const {
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  if (!ready() || impl_->cpu_processor == nullptr) {
    return false;
  }
  if (pixels.empty()) {
    return true;
  }
  if (pixels.size() >
      static_cast<std::size_t>(std::numeric_limits<long>::max()) ||
      pixels.size() > std::numeric_limits<std::size_t>::max() / 4U) {
    return false;
  }
  std::vector<float> packed;
  try {
    packed.resize(pixels.size() * 4U);
  } catch (const std::exception&) {
    return false;
  }
  for (std::size_t index = 0U; index < pixels.size(); ++index) {
    packed[index * 4U] = pixels[index].red;
    packed[index * 4U + 1U] = pixels[index].green;
    packed[index * 4U + 2U] = pixels[index].blue;
    packed[index * 4U + 3U] = pixels[index].alpha;
  }
  try {
    OCIO::PackedImageDesc image(
        packed.data(),
        static_cast<long>(pixels.size()),
        1,
        4);
    impl_->cpu_processor->apply(image);
  } catch (const OCIO::Exception&) {
    return false;
  }
  for (std::size_t index = 0U; index < pixels.size(); ++index) {
    const auto offset = index * 4U;
    if (!std::isfinite(packed[offset]) ||
        !std::isfinite(packed[offset + 1U]) ||
        !std::isfinite(packed[offset + 2U]) ||
        !std::isfinite(packed[offset + 3U]) ||
        packed[offset + 3U] != pixels[index].alpha) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < pixels.size(); ++index) {
    const auto offset = index * 4U;
    pixels[index].red = packed[offset];
    pixels[index].green = packed[offset + 1U];
    pixels[index].blue = packed[offset + 2U];
  }
  return true;
#else
  static_cast<void>(pixels);
  return false;
#endif
}

OcioProjectGpuProgram OcioProjectProcessor::compile_msl_gpu_program() const {
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  if (!ready() || impl_->processor == nullptr) {
    return failed_gpu_program(
        description(),
        OcioProjectStatus::InvalidRequest,
        "processor_not_ready");
  }
  try {
    const auto gpu_processor = impl_->processor->getDefaultGPUProcessor();
    if (gpu_processor == nullptr) {
      return failed_gpu_program(
          description(),
          OcioProjectStatus::ProcessorCompileFailed,
          "opencolorio_returned_null_gpu_processor");
    }
    auto shader = OCIO::GpuShaderDesc::CreateShaderDesc();
    if (shader == nullptr) {
      return failed_gpu_program(
          description(),
          OcioProjectStatus::ProcessorCompileFailed,
          "opencolorio_returned_null_gpu_shader_descriptor");
    }
    const auto identity_hash = sha256_string(
        description().config_cache_id + ":" +
        description().processor_cache_id);
    const auto resource_prefix = "peocio_" + identity_hash.substr(0U, 16U) + "_";
    shader->setLanguage(OCIO::GPU_LANGUAGE_MSL_2_0);
    shader->setFunctionName("photo_editor_ocio_transform");
    shader->setResourcePrefix(resource_prefix.c_str());
    shader->setTextureMaxWidth(16384U);
    shader->setAllowTexture1D(true);
    gpu_processor->extractGpuShaderInfo(shader);

    auto compiled = std::make_shared<OcioProjectGpuProgram::Impl>();
    auto& output = compiled->description;
    output.config_cache_id = description().config_cache_id;
    output.processor_cache_id = description().processor_cache_id;
    output.gpu_processor_cache_id = safe_string(gpu_processor->getCacheID());
    output.shader_cache_id = safe_string(shader->getCacheID());
    output.function_name = "photo_editor_ocio_transform";
    output.resource_prefix = resource_prefix;
    output.uniform_count = shader->getNumUniforms();
    output.uniform_buffer_bytes = shader->getUniformBufferSize();
    if (output.uniform_count != 0U || output.uniform_buffer_bytes != 0U) {
      output.status = OcioProjectStatus::InvalidRequest;
      output.failure_reason = "dynamic_gpu_uniforms_not_supported";
      return OcioProjectGpuProgram(std::move(compiled));
    }
    output.shader_text = safe_string(shader->getShaderText());
    if (output.shader_text.empty() ||
        output.shader_text.size() > kMaximumOcioGpuShaderBytes) {
      output.status = OcioProjectStatus::ProcessorCompileFailed;
      output.failure_reason = "gpu_shader_source_invalid_or_too_large";
      return OcioProjectGpuProgram(std::move(compiled));
    }
    output.shader_sha256 = sha256_string(output.shader_text);

    const auto texture_count =
        static_cast<std::size_t>(shader->getNumTextures()) +
        static_cast<std::size_t>(shader->getNum3DTextures());
    if (texture_count > kMaximumOcioGpuTextures) {
      output.status = OcioProjectStatus::ProcessorCompileFailed;
      output.failure_reason = "gpu_texture_count_exceeds_limit";
      return OcioProjectGpuProgram(std::move(compiled));
    }
    output.textures.reserve(texture_count);
    for (unsigned index = 0U; index < shader->getNumTextures(); ++index) {
      const char* texture_name = nullptr;
      const char* sampler_name = nullptr;
      unsigned width = 0U;
      unsigned height = 0U;
      OCIO::GpuShaderCreator::TextureType channel =
          OCIO::GpuShaderCreator::TEXTURE_RGB_CHANNEL;
      OCIO::GpuShaderCreator::TextureDimensions dimensions =
          OCIO::GpuShaderCreator::TEXTURE_1D;
      OCIO::Interpolation interpolation = OCIO::INTERP_UNKNOWN;
      shader->getTexture(
          index,
          texture_name,
          sampler_name,
          width,
          height,
          channel,
          dimensions,
          interpolation);
      const float* values = nullptr;
      shader->getTextureValues(index, values);
      const auto channels =
          channel == OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL ? 1U : 3U;
      std::size_t value_count = 0U;
      if (values == nullptr || width == 0U || height == 0U ||
          !checked_product(
              {static_cast<std::size_t>(width),
               static_cast<std::size_t>(height),
               channels},
              &value_count) ||
          value_count >
              (kMaximumOcioGpuTextureBytes - output.texture_bytes) /
                  sizeof(float)) {
        output.status = OcioProjectStatus::ProcessorCompileFailed;
        output.failure_reason = "gpu_texture_payload_invalid_or_too_large";
        return OcioProjectGpuProgram(std::move(compiled));
      }
      output.texture_bytes += value_count * sizeof(float);
      output.textures.push_back({
          .name = safe_string(texture_name),
          .sampler_name = safe_string(sampler_name),
          .kind = dimensions == OCIO::GpuShaderCreator::TEXTURE_1D
                      ? OcioProjectGpuTextureKind::OneDimensional
                      : OcioProjectGpuTextureKind::TwoDimensional,
          .binding_index = shader->getTextureShaderBindingIndex(index),
          .width = width,
          .height = height,
          .depth = 1U,
          .channels = channels,
          .interpolation = safe_string(OCIO::InterpolationToString(interpolation)),
          .values = std::vector<float>(values, values + value_count),
      });
    }
    for (unsigned index = 0U; index < shader->getNum3DTextures(); ++index) {
      const char* texture_name = nullptr;
      const char* sampler_name = nullptr;
      unsigned edge_length = 0U;
      OCIO::Interpolation interpolation = OCIO::INTERP_UNKNOWN;
      shader->get3DTexture(
          index,
          texture_name,
          sampler_name,
          edge_length,
          interpolation);
      const float* values = nullptr;
      shader->get3DTextureValues(index, values);
      std::size_t value_count = 0U;
      if (values == nullptr || edge_length == 0U ||
          !checked_product(
              {static_cast<std::size_t>(edge_length),
               static_cast<std::size_t>(edge_length),
               static_cast<std::size_t>(edge_length),
               std::size_t{3U}},
              &value_count) ||
          value_count >
              (kMaximumOcioGpuTextureBytes - output.texture_bytes) /
                  sizeof(float)) {
        output.status = OcioProjectStatus::ProcessorCompileFailed;
        output.failure_reason = "gpu_3d_texture_payload_invalid_or_too_large";
        return OcioProjectGpuProgram(std::move(compiled));
      }
      output.texture_bytes += value_count * sizeof(float);
      output.textures.push_back({
          .name = safe_string(texture_name),
          .sampler_name = safe_string(sampler_name),
          .kind = OcioProjectGpuTextureKind::ThreeDimensional,
          .binding_index = shader->get3DTextureShaderBindingIndex(index),
          .width = edge_length,
          .height = edge_length,
          .depth = edge_length,
          .channels = 3U,
          .interpolation = safe_string(OCIO::InterpolationToString(interpolation)),
          .values = std::vector<float>(values, values + value_count),
      });
    }
    auto resource_manifest = std::string("ocio-project-gpu-resources:v1|");
    for (const auto& texture : output.textures) {
      resource_manifest += std::to_string(texture.name.size()) + ":" +
                           texture.name + "|" +
                           std::to_string(texture.binding_index) + "|" +
                           std::to_string(texture.width) + "x" +
                           std::to_string(texture.height) + "x" +
                           std::to_string(texture.depth) + "x" +
                           std::to_string(texture.channels) + "|" +
                           texture.interpolation + "|";
      resource_manifest += color_payload_sha256(
          std::as_bytes(std::span(texture.values)));
      resource_manifest += '|';
    }
    output.resource_sha256 = sha256_string(resource_manifest);
    output.program_cache_key = sha256_string(
        output.config_cache_id + ":" + output.processor_cache_id + ":" +
        output.gpu_processor_cache_id + ":" + output.shader_cache_id + ":" +
        output.shader_sha256 + ":" + output.resource_sha256);
    output.status = OcioProjectStatus::Ready;
    return OcioProjectGpuProgram(std::move(compiled));
  } catch (const OCIO::Exception& error) {
    return failed_gpu_program(
        description(),
        OcioProjectStatus::ProcessorCompileFailed,
        error.what());
  } catch (const std::exception& error) {
    return failed_gpu_program(
        description(),
        OcioProjectStatus::ProcessorCompileFailed,
        error.what());
  }
#else
  return failed_gpu_program(
      description(),
      OcioProjectStatus::BackendUnavailable,
      "opencolorio_backend_unavailable");
#endif
}

OcioProjectConfig::OcioProjectConfig() : impl_(std::make_shared<Impl>()) {}
OcioProjectConfig::~OcioProjectConfig() = default;
OcioProjectConfig::OcioProjectConfig(const OcioProjectConfig&) = default;
OcioProjectConfig& OcioProjectConfig::operator=(const OcioProjectConfig&) = default;
OcioProjectConfig::OcioProjectConfig(OcioProjectConfig&&) noexcept = default;
OcioProjectConfig& OcioProjectConfig::operator=(OcioProjectConfig&&) noexcept = default;
OcioProjectConfig::OcioProjectConfig(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

bool OcioProjectConfig::ready() const {
  return impl_ != nullptr && impl_->description.status == OcioProjectStatus::Ready;
}

const OcioProjectConfigDescription& OcioProjectConfig::description() const {
  static const OcioProjectConfigDescription unavailable;
  return impl_ == nullptr ? unavailable : impl_->description;
}

OcioProjectFileRuleMatch OcioProjectConfig::match_file_rule(
    std::string_view file_path) const {
  OcioProjectFileRuleMatch match;
  match.file_path = std::string(file_path);
  if (!ready() || file_path.empty()) {
    match.failure_reason = ready() ? "file_path_empty" : "config_not_ready";
    return match;
  }
  if (!dependencies_unchanged(*impl_)) {
    match.failure_reason = "config_inputs_changed_reload_required";
    return match;
  }
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  try {
    std::size_t index = 0U;
    match.color_space = safe_string(
        impl_->config->getColorSpaceFromFilepath(match.file_path.c_str(), index));
    const auto rules = impl_->config->getFileRules();
    if (match.color_space.empty() || rules == nullptr ||
        index >= rules->getNumEntries()) {
      match.failure_reason = "file_rule_did_not_resolve_color_space";
      return match;
    }
    match.rule_index = index;
    match.rule_name = safe_string(rules->getName(index));
    match.only_default_rule =
        impl_->config->filepathOnlyMatchesDefaultRule(match.file_path.c_str());
    match.status = OcioProjectStatus::Ready;
    return match;
  } catch (const OCIO::Exception& error) {
    match.failure_reason = error.what();
    return match;
  }
#else
  match.status = OcioProjectStatus::BackendUnavailable;
  match.failure_reason = "opencolorio_backend_unavailable";
  return match;
#endif
}

OcioProjectProcessor OcioProjectConfig::compile_processor(
    const OcioProjectProcessorRequest& request) const {
  const auto cache_id = description().config_cache_id;
  if (!ready()) {
    return failed_processor(
        request,
        cache_id,
        OcioProjectStatus::InvalidRequest,
        "config_not_ready");
  }
  if (!dependencies_unchanged(*impl_)) {
    return failed_processor(
        request,
        cache_id,
        OcioProjectStatus::InvalidRequest,
        "config_inputs_changed_reload_required");
  }
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  try {
    if (safe_string(impl_->config->getCacheID(impl_->context)) != cache_id) {
      return failed_processor(
          request,
          cache_id,
          OcioProjectStatus::InvalidRequest,
          "config_cache_identity_changed_reload_required");
    }
    OCIO::ConstProcessorRcPtr processor;
    std::string file_transform_sha256_before;
    const auto direction = request.inverse ? OCIO::TRANSFORM_DIR_INVERSE
                                           : OCIO::TRANSFORM_DIR_FORWARD;
    switch (request.kind) {
      case OcioProjectProcessorKind::ColorSpace:
        if (request.source_color_space.empty() ||
            request.destination_color_space.empty()) {
          return failed_processor(
              request,
              cache_id,
              OcioProjectStatus::InvalidRequest,
              "color_space_names_required");
        }
        processor = request.inverse
                        ? impl_->config->getProcessor(
                              impl_->context,
                              request.destination_color_space.c_str(),
                              request.source_color_space.c_str())
                        : impl_->config->getProcessor(
                              impl_->context,
                              request.source_color_space.c_str(),
                              request.destination_color_space.c_str());
        break;
      case OcioProjectProcessorKind::DisplayView:
        if (request.source_color_space.empty() || request.display.empty() ||
            request.view.empty()) {
          return failed_processor(
              request,
              cache_id,
              OcioProjectStatus::InvalidRequest,
              "source_display_and_view_required");
        }
        processor = impl_->config->getProcessor(
            impl_->context,
            request.source_color_space.c_str(),
            request.display.c_str(),
            request.view.c_str(),
            direction);
        break;
      case OcioProjectProcessorKind::Look: {
        if (request.source_color_space.empty() ||
            request.destination_color_space.empty() || request.looks.empty()) {
          return failed_processor(
              request,
              cache_id,
              OcioProjectStatus::InvalidRequest,
              "source_destination_and_looks_required");
        }
        const auto transform = OCIO::LookTransform::Create();
        transform->setSrc(request.source_color_space.c_str());
        transform->setDst(request.destination_color_space.c_str());
        transform->setLooks(request.looks.c_str());
        transform->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
        transform->validate();
        processor = impl_->config->getProcessor(
            impl_->context,
            transform,
            direction);
        break;
      }
      case OcioProjectProcessorKind::FileTransform: {
        if (request.file_transform_path.empty()) {
          return failed_processor(
              request,
              cache_id,
              OcioProjectStatus::InvalidRequest,
              "file_transform_path_required");
        }
        const std::filesystem::path file_path(request.file_transform_path);
        std::error_code file_error;
        if (!std::filesystem::is_regular_file(file_path, file_error) ||
            file_error ||
            !OCIO::FileTransform::IsFormatExtensionSupported(
                lower_extension(file_path).c_str())) {
          return failed_processor(
              request,
              cache_id,
              OcioProjectStatus::InvalidRequest,
              "unsupported_or_unavailable_file_transform");
        }
        file_transform_sha256_before = file_sha256(file_path);
        if (file_transform_sha256_before.empty()) {
          return failed_processor(
              request,
              cache_id,
              OcioProjectStatus::ProcessorCompileFailed,
              "file_transform_provenance_unavailable");
        }
        const auto transform = OCIO::FileTransform::Create();
        transform->setSrc(file_path.string().c_str());
        transform->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
        transform->validate();
        processor = impl_->config->getProcessor(
            impl_->context,
            transform,
            direction);
        break;
      }
    }
    if (processor == nullptr) {
      return failed_processor(
          request,
          cache_id,
          OcioProjectStatus::ProcessorCompileFailed,
          "opencolorio_returned_null_processor");
    }
    auto compiled = std::make_shared<OcioProjectProcessor::Impl>();
    compiled->description.status = OcioProjectStatus::Ready;
    compiled->description.kind = request.kind;
    compiled->description.config_cache_id = cache_id;
    compiled->description.processor_cache_id = safe_string(processor->getCacheID());
    compiled->description.source_color_space = request.source_color_space;
    compiled->description.destination_color_space = request.destination_color_space;
    compiled->description.display = request.display;
    compiled->description.view = request.view;
    compiled->description.looks = request.looks;
    compiled->description.file_transform_path = request.file_transform_path;
    compiled->description.inverse = request.inverse;
    compiled->description.no_op = processor->isNoOp();
    compiled->description.has_channel_crosstalk =
        processor->hasChannelCrosstalk();
    const auto metadata = processor->getProcessorMetadata();
    if (metadata != nullptr) {
      for (int index = 0; index < metadata->getNumLooks(); ++index) {
        compiled->description.resolved_looks.push_back(
            safe_string(metadata->getLook(index)));
      }
      for (int index = 0; index < metadata->getNumFiles(); ++index) {
        const auto path = safe_string(metadata->getFile(index));
        auto sha256 = file_sha256(std::filesystem::path(path));
        auto provenance_scope = std::string("file-content");
        if (sha256.empty() &&
            impl_->description.source_kind == "ocioz-archive") {
          sha256 = impl_->description.source_sha256;
          provenance_scope = "containing-archive";
        }
        if (path.empty() || sha256.empty()) {
          return failed_processor(
              request,
              cache_id,
              OcioProjectStatus::ProcessorCompileFailed,
              "external_transform_provenance_unavailable");
        }
        compiled->description.external_files.push_back({
            .path = path,
            .sha256 = sha256,
            .provenance_scope = provenance_scope,
        });
      }
    }
    if (request.kind == OcioProjectProcessorKind::FileTransform &&
        file_sha256(request.file_transform_path) !=
            file_transform_sha256_before) {
      return failed_processor(
          request,
          cache_id,
          OcioProjectStatus::ProcessorCompileFailed,
          "file_transform_changed_during_compilation");
    }
    if (safe_string(impl_->config->getCacheID(impl_->context)) != cache_id ||
        !source_provenance_unchanged(*impl_)) {
      return failed_processor(
          request,
          cache_id,
          OcioProjectStatus::ProcessorCompileFailed,
          "config_inputs_changed_during_compilation");
    }
    if (request.kind != OcioProjectProcessorKind::FileTransform) {
      const std::lock_guard lock(impl_->dependency_mutex);
      for (const auto& file : compiled->description.external_files) {
        if (file.provenance_scope != "file-content") {
          continue;
        }
        const auto existing = impl_->dependency_hashes.find(file.path);
        if (existing != impl_->dependency_hashes.end() &&
            existing->second != file.sha256) {
          return failed_processor(
              request,
              cache_id,
              OcioProjectStatus::ProcessorCompileFailed,
              "config_dependency_changed_during_compilation");
        }
        impl_->dependency_hashes[file.path] = file.sha256;
      }
    }
    compiled->processor = processor;
    compiled->cpu_processor = processor->getDefaultCPUProcessor();
    if (compiled->cpu_processor == nullptr) {
      return failed_processor(
          request,
          cache_id,
          OcioProjectStatus::ProcessorCompileFailed,
          "opencolorio_returned_null_cpu_processor");
    }
    return OcioProjectProcessor(std::move(compiled));
  } catch (const OCIO::Exception& error) {
    return failed_processor(
        request,
        cache_id,
        OcioProjectStatus::ProcessorCompileFailed,
        error.what());
  }
#else
  return failed_processor(
      request,
      cache_id,
      OcioProjectStatus::BackendUnavailable,
      "opencolorio_backend_unavailable");
#endif
}

bool ocio_project_backend_available() {
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  return true;
#else
  return false;
#endif
}

std::string_view ocio_project_backend_version() {
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  return PHOTO_EDITOR_OCIO_COMPILED_VERSION;
#else
  return "unavailable";
#endif
}

OcioProjectConfig load_ocio_project_config(std::string_view source_path) {
  return load_ocio_project_config(source_path, {});
}

OcioProjectConfig load_ocio_project_config(
    std::string_view source_path,
    const OcioProjectLoadOptions& options) {
  auto impl = std::make_shared<OcioProjectConfig::Impl>();
  impl->description.backend_version =
      std::string(ocio_project_backend_version());
  impl->description.source_path = std::string(source_path);
  impl->description.source_kind = config_source_kind(source_path);
  for (std::size_t index = 0U;
       index < options.context_overrides.size();
       ++index) {
    const auto& variable = options.context_overrides[index];
    if (variable.name.empty() ||
        std::any_of(
            options.context_overrides.begin(),
            options.context_overrides.begin() +
                static_cast<std::ptrdiff_t>(index),
            [&](const OcioProjectContextOverride& existing) {
              return existing.name == variable.name;
            })) {
      impl->description.status = OcioProjectStatus::InvalidRequest;
      impl->description.failure_reason =
          variable.name.empty() ? "ocio_context_variable_name_empty"
                                : "ocio_context_variable_duplicate";
      return OcioProjectConfig(std::move(impl));
    }
  }
  if (source_path.empty()) {
    impl->description.status = OcioProjectStatus::PathEmpty;
    impl->description.failure_reason = "ocio_project_config_path_empty";
    return OcioProjectConfig(std::move(impl));
  }
  if (!supported_config_source(source_path)) {
    impl->description.status = OcioProjectStatus::UnsupportedSource;
    impl->description.failure_reason = "unsupported_ocio_project_config_source";
    return OcioProjectConfig(std::move(impl));
  }
  if (!source_path.starts_with("ocio://")) {
    const std::filesystem::path path(source_path);
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(path, file_error) || file_error) {
      impl->description.status = OcioProjectStatus::FileUnavailable;
      impl->description.failure_reason = "ocio_project_config_file_unavailable";
      return OcioProjectConfig(std::move(impl));
    }
    std::error_code canonical_error;
    const auto canonical = std::filesystem::weakly_canonical(path, canonical_error);
    if (!canonical_error) {
      impl->description.source_path = canonical.string();
    }
    impl->description.source_sha256 = file_sha256(path);
    if (impl->description.source_sha256.empty()) {
      impl->description.status = OcioProjectStatus::FileUnavailable;
      impl->description.failure_reason =
          "ocio_project_config_provenance_unavailable";
      return OcioProjectConfig(std::move(impl));
    }
  }
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  try {
    impl->config = OCIO::Config::CreateFromFile(
        impl->description.source_path.c_str());
  } catch (const OCIO::Exception& error) {
    impl->description.status = OcioProjectStatus::ParseFailed;
    impl->description.failure_reason = error.what();
    return OcioProjectConfig(std::move(impl));
  }
  try {
    auto context = impl->config->getCurrentContext()->createEditableCopy();
    context->clearStringVars();
    // Retain OCIO's declared-variable mode for processor semantics, but never
    // call loadEnvironment(): effective values come only from the config and
    // the explicit override list below.
    context->setEnvironmentMode(OCIO::ENV_ENVIRONMENT_LOAD_PREDEFINED);
    for (int index = 0; index < impl->config->getNumEnvironmentVars(); ++index) {
      const auto name = safe_string(
          impl->config->getEnvironmentVarNameByIndex(index));
      const auto value = safe_string(
          impl->config->getEnvironmentVarDefault(name.c_str()));
      if (!name.empty()) {
        context->setStringVar(name.c_str(), value.c_str());
        impl->description.context_variables.push_back({
            .name = name,
            .value = value,
            .source = "config-default",
        });
      }
    }
    for (const auto& variable : options.context_overrides) {
      context->setStringVar(variable.name.c_str(), variable.value.c_str());
      const auto existing = std::find_if(
          impl->description.context_variables.begin(),
          impl->description.context_variables.end(),
          [&](const OcioProjectContextVariable& value) {
            return value.name == variable.name;
          });
      if (existing == impl->description.context_variables.end()) {
        impl->description.context_variables.push_back({
            .name = variable.name,
            .value = variable.value,
            .source = "explicit-override",
        });
      } else {
        existing->value = variable.value;
        existing->source = "explicit-override";
      }
    }
    impl->context = context;
    impl->config->validate();
    impl->description.config_name = safe_string(impl->config->getName());
    impl->description.config_description =
        safe_string(impl->config->getDescription());
    impl->description.context_cache_id = safe_string(context->getCacheID());
    impl->description.config_cache_id = safe_string(
        impl->config->getCacheID(impl->context));
    impl->description.default_display =
        safe_string(impl->config->getDefaultDisplay());
    impl->description.strict_parsing =
        impl->config->isStrictParsingEnabled();
    for (int index = 0; index < impl->config->getNumRoles(); ++index) {
      impl->description.roles.push_back({
          .name = safe_string(impl->config->getRoleName(index)),
          .color_space = safe_string(impl->config->getRoleColorSpace(index)),
      });
    }
    for (int index = 0; index < impl->config->getNumColorSpaces(); ++index) {
      const auto name = safe_string(impl->config->getColorSpaceNameByIndex(index));
      const auto color_space = impl->config->getColorSpace(name.c_str());
      if (color_space == nullptr) {
        continue;
      }
      impl->description.color_spaces.push_back({
          .name = name,
          .family = safe_string(color_space->getFamily()),
          .description = safe_string(color_space->getDescription()),
          .encoding = safe_string(color_space->getEncoding()),
          .reference_space =
              color_space->getReferenceSpaceType() == OCIO::REFERENCE_SPACE_DISPLAY
                  ? "display"
                  : "scene",
          .is_data = color_space->isData(),
      });
    }
    for (int display_index = 0;
         display_index < impl->config->getNumDisplays();
         ++display_index) {
      const auto display = safe_string(impl->config->getDisplay(display_index));
      const auto default_view =
          safe_string(impl->config->getDefaultView(display.c_str()));
      for (int view_index = 0;
           view_index < impl->config->getNumViews(display.c_str());
           ++view_index) {
        const auto view =
            safe_string(impl->config->getView(display.c_str(), view_index));
        impl->description.views.push_back({
            .display = display,
            .name = view,
            .view_transform = safe_string(
                impl->config->getDisplayViewTransformName(
                    display.c_str(), view.c_str())),
            .color_space = safe_string(
                impl->config->getDisplayViewColorSpaceName(
                    display.c_str(), view.c_str())),
            .looks = safe_string(impl->config->getDisplayViewLooks(
                display.c_str(), view.c_str())),
            .rule = safe_string(impl->config->getDisplayViewRule(
                display.c_str(), view.c_str())),
            .description = safe_string(
                impl->config->getDisplayViewDescription(
                    display.c_str(), view.c_str())),
            .is_default = view == default_view,
            .is_shared = impl->config->isViewShared(
                display.c_str(), view.c_str()),
        });
      }
    }
    for (int index = 0; index < impl->config->getNumLooks(); ++index) {
      const auto name = safe_string(impl->config->getLookNameByIndex(index));
      const auto look = impl->config->getLook(name.c_str());
      if (look == nullptr) {
        continue;
      }
      impl->description.looks.push_back({
          .name = name,
          .process_space = safe_string(look->getProcessSpace()),
          .description = safe_string(look->getDescription()),
          .has_forward_transform = look->getTransform() != nullptr,
          .has_inverse_transform = look->getInverseTransform() != nullptr,
      });
    }
    const auto rules = impl->config->getFileRules();
    if (rules != nullptr) {
      for (std::size_t index = 0U; index < rules->getNumEntries(); ++index) {
        impl->description.file_rules.push_back({
            .index = index,
            .name = safe_string(rules->getName(index)),
            .pattern = safe_string(rules->getPattern(index)),
            .extension = safe_string(rules->getExtension(index)),
            .regex = safe_string(rules->getRegex(index)),
            .color_space = safe_string(rules->getColorSpace(index)),
        });
      }
    }
    if (!source_provenance_unchanged(*impl)) {
      impl->config.reset();
      impl->description.status = OcioProjectStatus::ValidationFailed;
      impl->description.failure_reason =
          "ocio_project_config_changed_during_load";
      return OcioProjectConfig(std::move(impl));
    }
    impl->description.status = OcioProjectStatus::Ready;
    return OcioProjectConfig(std::move(impl));
  } catch (const OCIO::Exception& error) {
    impl->config.reset();
    impl->description.status = OcioProjectStatus::ValidationFailed;
    impl->description.failure_reason = error.what();
    return OcioProjectConfig(std::move(impl));
  }
#else
  impl->description.status = OcioProjectStatus::BackendUnavailable;
  impl->description.failure_reason = "opencolorio_backend_unavailable";
  return OcioProjectConfig(std::move(impl));
#endif
}

OcioProjectArchiveResult archive_ocio_project_config(
    std::string_view source_path,
    std::string_view archive_path) {
  OcioProjectArchiveResult result;
  result.source_path = std::string(source_path);
  result.archive_path = std::string(archive_path);
  if (source_path.empty() || archive_path.empty()) {
    result.status = OcioProjectStatus::PathEmpty;
    result.failure_reason = "ocio_archive_path_empty";
    return result;
  }
  const std::filesystem::path source(source_path);
  const std::filesystem::path destination(archive_path);
  if (lower_extension(source) != ".ocio" ||
      lower_extension(destination) != ".ocioz") {
    result.status = OcioProjectStatus::UnsupportedSource;
    result.failure_reason = "ocio_archive_requires_ocio_source_and_ocioz_destination";
    return result;
  }
  std::error_code destination_error;
  const auto destination_exists =
      std::filesystem::exists(destination, destination_error);
  if (destination_error) {
    result.status = OcioProjectStatus::ArchiveFailed;
    result.failure_reason = "ocio_archive_destination_uninspectable";
    return result;
  }
  if (destination_exists) {
    result.status = OcioProjectStatus::ArchiveFailed;
    result.failure_reason = "ocio_archive_destination_exists";
    return result;
  }
  const auto config = load_ocio_project_config(source_path);
  if (!config.ready()) {
    result.status = config.description().status;
    result.failure_reason = config.description().failure_reason;
    return result;
  }
#if defined(PHOTO_EDITOR_HAS_OCIO) && PHOTO_EDITOR_HAS_OCIO
  const auto partial = destination.string() + ".partial";
  std::error_code partial_error;
  const auto partial_exists = std::filesystem::exists(partial, partial_error);
  if (partial_error) {
    result.status = OcioProjectStatus::ArchiveFailed;
    result.failure_reason = "ocio_archive_partial_destination_uninspectable";
    return result;
  }
  if (partial_exists) {
    result.status = OcioProjectStatus::ArchiveFailed;
    result.failure_reason = "ocio_archive_partial_destination_exists";
    return result;
  }
  try {
    {
      std::ofstream output(partial, std::ios::binary | std::ios::trunc);
      if (!output) {
        result.status = OcioProjectStatus::ArchiveFailed;
        result.failure_reason = "ocio_archive_destination_unwritable";
        return result;
      }
      config.impl_->config->archive(output);
      output.close();
      if (!output) {
        std::filesystem::remove(partial);
        result.status = OcioProjectStatus::ArchiveFailed;
        result.failure_reason = "ocio_archive_write_failed";
        return result;
      }
    }
    std::filesystem::rename(partial, destination);
    result.archive_sha256 = file_sha256(destination);
    if (result.archive_sha256.empty()) {
      std::filesystem::remove(destination);
      result.status = OcioProjectStatus::ArchiveFailed;
      result.failure_reason = "ocio_archive_hash_failed";
      return result;
    }
    result.status = OcioProjectStatus::Ready;
    return result;
  } catch (const std::exception& error) {
    std::error_code remove_error;
    std::filesystem::remove(partial, remove_error);
    result.status = OcioProjectStatus::ArchiveFailed;
    result.failure_reason = error.what();
    return result;
  }
#else
  result.status = OcioProjectStatus::BackendUnavailable;
  result.failure_reason = "opencolorio_backend_unavailable";
  return result;
#endif
}

}  // namespace photo_editor::pipeline
