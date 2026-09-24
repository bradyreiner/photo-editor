#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

enum class OcioProjectStatus {
  Ready,
  BackendUnavailable,
  PathEmpty,
  FileUnavailable,
  UnsupportedSource,
  ParseFailed,
  ValidationFailed,
  InvalidRequest,
  ProcessorCompileFailed,
  ApplyFailed,
  ArchiveFailed,
};

[[nodiscard]] const char* ocio_project_status_name(OcioProjectStatus status);

struct OcioProjectRole final {
  std::string name;
  std::string color_space;
};

struct OcioProjectColorSpace final {
  std::string name;
  std::string family;
  std::string description;
  std::string encoding;
  std::string reference_space;
  bool is_data = false;
};

struct OcioProjectView final {
  std::string display;
  std::string name;
  std::string view_transform;
  std::string color_space;
  std::string looks;
  std::string rule;
  std::string description;
  bool is_default = false;
  bool is_shared = false;
};

struct OcioProjectLook final {
  std::string name;
  std::string process_space;
  std::string description;
  bool has_forward_transform = false;
  bool has_inverse_transform = false;
};

struct OcioProjectFileRule final {
  std::size_t index = 0U;
  std::string name;
  std::string pattern;
  std::string extension;
  std::string regex;
  std::string color_space;
};

struct OcioProjectContextVariable final {
  std::string name;
  std::string value;
  std::string source;
};

struct OcioProjectContextOverride final {
  std::string name;
  std::string value;
};

struct OcioProjectLoadOptions final {
  std::vector<OcioProjectContextOverride> context_overrides;
};

struct OcioProjectConfigDescription final {
  std::string schema = "ocio-project-config:v1";
  OcioProjectStatus status = OcioProjectStatus::BackendUnavailable;
  std::string backend = "OpenColorIO";
  std::string backend_version;
  std::string source_path;
  std::string source_kind;
  std::string source_sha256;
  std::string config_name;
  std::string config_description;
  std::string config_cache_id;
  std::string context_cache_id;
  std::string default_display;
  bool strict_parsing = false;
  bool process_environment_ignored = true;
  bool photographic_pipeline_isolated = true;
  std::vector<OcioProjectRole> roles;
  std::vector<OcioProjectColorSpace> color_spaces;
  std::vector<OcioProjectView> views;
  std::vector<OcioProjectLook> looks;
  std::vector<OcioProjectFileRule> file_rules;
  std::vector<OcioProjectContextVariable> context_variables;
  std::string failure_reason;
};

enum class OcioProjectProcessorKind {
  ColorSpace,
  DisplayView,
  Look,
  FileTransform,
};

struct OcioProjectProcessorRequest final {
  OcioProjectProcessorKind kind = OcioProjectProcessorKind::ColorSpace;
  std::string source_color_space;
  std::string destination_color_space;
  std::string display;
  std::string view;
  std::string looks;
  std::string file_transform_path;
  bool inverse = false;
};

struct OcioProjectExternalFile final {
  std::string path;
  std::string sha256;
  std::string provenance_scope;
};

struct OcioProjectProcessorDescription final {
  std::string schema = "ocio-project-processor:v1";
  OcioProjectStatus status = OcioProjectStatus::BackendUnavailable;
  OcioProjectProcessorKind kind = OcioProjectProcessorKind::ColorSpace;
  std::string config_cache_id;
  std::string processor_cache_id;
  std::string source_color_space;
  std::string destination_color_space;
  std::string display;
  std::string view;
  std::string looks;
  std::string file_transform_path;
  bool inverse = false;
  bool no_op = false;
  bool has_channel_crosstalk = false;
  bool preserves_unassociated_alpha = true;
  std::vector<std::string> resolved_looks;
  std::vector<OcioProjectExternalFile> external_files;
  std::string failure_reason;
};

struct OcioProjectRgbaFloat final {
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
  float alpha = 1.0F;
};

enum class OcioProjectGpuTextureKind {
  OneDimensional,
  TwoDimensional,
  ThreeDimensional,
};

struct OcioProjectGpuTexture final {
  std::string name;
  std::string sampler_name;
  OcioProjectGpuTextureKind kind =
      OcioProjectGpuTextureKind::OneDimensional;
  std::size_t binding_index = 0U;
  std::size_t width = 0U;
  std::size_t height = 0U;
  std::size_t depth = 0U;
  std::size_t channels = 0U;
  std::string interpolation;
  std::vector<float> values;
};

struct OcioProjectGpuProgramDescription final {
  std::string schema = "ocio-project-gpu-program:v1";
  OcioProjectStatus status = OcioProjectStatus::BackendUnavailable;
  std::string language = "msl-2.0";
  std::string function_name;
  std::string resource_prefix;
  std::string config_cache_id;
  std::string processor_cache_id;
  std::string gpu_processor_cache_id;
  std::string shader_cache_id;
  std::string shader_sha256;
  std::string resource_sha256;
  std::string program_cache_key;
  std::string shader_text;
  std::size_t uniform_count = 0U;
  std::size_t uniform_buffer_bytes = 0U;
  std::size_t texture_bytes = 0U;
  bool preserves_unassociated_alpha = true;
  std::vector<OcioProjectGpuTexture> textures;
  std::string failure_reason;
};

class OcioProjectGpuProgram final {
 public:
  struct Impl;

  OcioProjectGpuProgram();
  ~OcioProjectGpuProgram();
  OcioProjectGpuProgram(const OcioProjectGpuProgram&);
  OcioProjectGpuProgram& operator=(const OcioProjectGpuProgram&);
  OcioProjectGpuProgram(OcioProjectGpuProgram&&) noexcept;
  OcioProjectGpuProgram& operator=(OcioProjectGpuProgram&&) noexcept;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] const OcioProjectGpuProgramDescription& description() const;

  explicit OcioProjectGpuProgram(std::shared_ptr<Impl> impl);

 private:
  std::shared_ptr<Impl> impl_;

  friend class OcioProjectProcessor;
};

struct OcioProjectFileRuleMatch final {
  OcioProjectStatus status = OcioProjectStatus::InvalidRequest;
  std::string file_path;
  std::size_t rule_index = 0U;
  std::string rule_name;
  std::string color_space;
  bool only_default_rule = false;
  std::string failure_reason;
};

class OcioProjectProcessor final {
 public:
  struct Impl;

  OcioProjectProcessor();
  ~OcioProjectProcessor();
  OcioProjectProcessor(const OcioProjectProcessor&);
  OcioProjectProcessor& operator=(const OcioProjectProcessor&);
  OcioProjectProcessor(OcioProjectProcessor&&) noexcept;
  OcioProjectProcessor& operator=(OcioProjectProcessor&&) noexcept;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] const OcioProjectProcessorDescription& description() const;
  [[nodiscard]] bool apply(std::span<OcioProjectRgbaFloat> pixels) const;
  [[nodiscard]] OcioProjectGpuProgram compile_msl_gpu_program() const;

  explicit OcioProjectProcessor(std::shared_ptr<Impl> impl);

 private:
  std::shared_ptr<Impl> impl_;

  friend class OcioProjectConfig;
};

struct OcioProjectArchiveResult;

class OcioProjectConfig final {
 public:
  struct Impl;

  OcioProjectConfig();
  ~OcioProjectConfig();
  OcioProjectConfig(const OcioProjectConfig&);
  OcioProjectConfig& operator=(const OcioProjectConfig&);
  OcioProjectConfig(OcioProjectConfig&&) noexcept;
  OcioProjectConfig& operator=(OcioProjectConfig&&) noexcept;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] const OcioProjectConfigDescription& description() const;
  [[nodiscard]] OcioProjectFileRuleMatch match_file_rule(
      std::string_view file_path) const;
  [[nodiscard]] OcioProjectProcessor compile_processor(
      const OcioProjectProcessorRequest& request) const;

 private:
  explicit OcioProjectConfig(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;

  friend OcioProjectConfig load_ocio_project_config(std::string_view);
  friend OcioProjectConfig load_ocio_project_config(
      std::string_view,
      const OcioProjectLoadOptions&);
  friend OcioProjectArchiveResult archive_ocio_project_config(
      std::string_view,
      std::string_view);
};

struct OcioProjectArchiveResult final {
  OcioProjectStatus status = OcioProjectStatus::BackendUnavailable;
  std::string source_path;
  std::string archive_path;
  std::string archive_sha256;
  std::string failure_reason;
};

[[nodiscard]] bool ocio_project_backend_available();
[[nodiscard]] std::string_view ocio_project_backend_version();
[[nodiscard]] OcioProjectConfig load_ocio_project_config(
    std::string_view source_path);
[[nodiscard]] OcioProjectConfig load_ocio_project_config(
    std::string_view source_path,
    const OcioProjectLoadOptions& options);
[[nodiscard]] OcioProjectArchiveResult archive_ocio_project_config(
    std::string_view source_path,
    std::string_view archive_path);

}  // namespace photo_editor::pipeline
