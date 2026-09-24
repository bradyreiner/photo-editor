#include "color_lut_runtime.h"

#include "color_working_space_runtime.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace photo_editor::pipeline {
namespace {

constexpr auto kFnvOffset = std::uint64_t{1469598103934665603ULL};
constexpr auto kFnvPrime = std::uint64_t{1099511628211ULL};

[[nodiscard]] std::uint64_t hash_append(
    std::uint64_t hash,
    std::uint64_t value) {
  for (auto byte = 0U; byte < 8U; ++byte) {
    hash ^= (value >> (byte * 8U)) & 0xffU;
    hash *= kFnvPrime;
  }
  return hash;
}

[[nodiscard]] bool valid_shape(int size, std::size_t table_size) {
  if (size < 2) return false;
  const auto edge = static_cast<std::size_t>(size);
  return edge <= std::numeric_limits<std::size_t>::max() / edge &&
         edge * edge <= std::numeric_limits<std::size_t>::max() / edge &&
         table_size == edge * edge * edge;
}

[[nodiscard]] bool finite_payload(const std::vector<ColorPixel>& table) {
  return std::ranges::all_of(table, [](const ColorPixel& pixel) {
    return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
           std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
  });
}

[[nodiscard]] bool payload_supported(
    int size,
    const std::vector<ColorPixel>& table,
    std::string_view input_color_space,
    std::string_view output_color_space) {
  return valid_shape(size, table.size()) && finite_payload(table) &&
         resolve_builtin_rgb_color_space(input_color_space).has_value() &&
         resolve_builtin_rgb_color_space(output_color_space).has_value();
}

[[nodiscard]] std::uint64_t table_identity(const ColorLut3D& lut) {
  auto hash = hash_append(kFnvOffset, static_cast<std::uint64_t>(lut.size));
  for (const auto value : lut.domain_min) {
    hash = hash_append(hash, std::bit_cast<std::uint64_t>(value));
  }
  for (const auto value : lut.domain_max) {
    hash = hash_append(hash, std::bit_cast<std::uint64_t>(value));
  }
  for (const auto& pixel : lut.table) {
    hash = hash_append(hash, std::bit_cast<std::uint32_t>(pixel.red));
    hash = hash_append(hash, std::bit_cast<std::uint32_t>(pixel.green));
    hash = hash_append(hash, std::bit_cast<std::uint32_t>(pixel.blue));
    hash = hash_append(hash, std::bit_cast<std::uint32_t>(pixel.alpha));
  }
  return hash == 0U ? 1U : hash;
}

[[nodiscard]] std::uint64_t compiled_identity(
    const CompiledUserLut& compiled) {
  auto identity = hash_append(kFnvOffset, compiled.lut.cache_identity);
  identity = hash_append(
      identity, std::bit_cast<std::uint32_t>(compiled.strength));
  for (const auto value : compiled.working_to_input.values) {
    identity = hash_append(identity, std::bit_cast<std::uint64_t>(value));
  }
  for (const auto value : compiled.output_to_working.values) {
    identity = hash_append(identity, std::bit_cast<std::uint64_t>(value));
  }
  identity = hash_append(
      identity, static_cast<std::uint64_t>(compiled.input_transfer));
  identity = hash_append(
      identity, static_cast<std::uint64_t>(compiled.output_transfer));
  return identity == 0U ? 1U : identity;
}

[[nodiscard]] ColorLut3D make_user_lut(
    int size,
    const std::array<double, 3>& domain_min,
    const std::array<double, 3>& domain_max,
    const std::vector<ColorPixel>& table,
    std::string_view input_color_space,
    std::string_view output_color_space) {
  return ColorLut3D{
      .table = table,
      .size = size,
      .domain_min = domain_min,
      .domain_max = domain_max,
      .input_color_space = std::string(input_color_space),
      .output_color_space = std::string(output_color_space),
      .valid = valid_shape(size, table.size()),
  };
}

[[nodiscard]] std::size_t requested_count(
    const ColorAdjustmentState& state) {
  auto count = std::size_t{0U};
  if (state.lut_enabled &&
      std::clamp(state.lut_strength / 100.0, 0.0, 1.0) > 1.0e-6) {
    ++count;
  }
  count += static_cast<std::size_t>(std::count_if(
      state.lut_stack.cbegin(), state.lut_stack.cend(), [](const auto& lut) {
        return lut.enabled &&
               std::clamp(lut.strength / 100.0, 0.0, 1.0) > 1.0e-6;
      }));
  return count;
}

}  // namespace

std::vector<CompiledUserLut> compile_user_lut_runtimes(
    const ColorAdjustmentState& state,
    const RgbColorSpace& working_space,
    std::string_view chromatic_adaptation_method) {
  auto luts = std::vector<CompiledUserLut>{};
  const auto append_lut = [&](ColorLut3D lut, double strength) {
    const auto input_space = resolve_builtin_rgb_color_space(lut.input_color_space);
    const auto output_space = resolve_builtin_rgb_color_space(lut.output_color_space);
    if (!payload_supported(
            lut.size, lut.table, lut.input_color_space,
            lut.output_color_space) ||
        !input_space.has_value() || !output_space.has_value()) {
      return;
    }
    lut.cache_identity = table_identity(lut);
    auto compiled = CompiledUserLut{
        .lut = std::move(lut),
        .strength = static_cast<float>(strength),
        .working_to_input = compile_builtin_color_transform(
                                working_space, *input_space,
                                chromatic_adaptation_method)
                                .matrix,
        .output_to_working = compile_builtin_color_transform(
                                *output_space, working_space,
                                chromatic_adaptation_method)
                                .matrix,
        .input_transfer = input_space->transfer_function,
        .output_transfer = output_space->transfer_function,
    };
    compiled.cache_identity = compiled_identity(compiled);
    luts.push_back(std::move(compiled));
  };
  const auto single_strength = std::clamp(state.lut_strength / 100.0, 0.0, 1.0);
  if (state.lut_enabled && single_strength > 1.0e-6) {
    append_lut(
        make_user_lut(
            state.lut_size, state.lut_domain_min, state.lut_domain_max,
            state.lut_table, state.lut_input_color_space,
            state.lut_output_color_space),
        single_strength);
  }
  for (const auto& adjustment : state.lut_stack) {
    const auto strength = std::clamp(adjustment.strength / 100.0, 0.0, 1.0);
    if (!adjustment.enabled || strength <= 1.0e-6) continue;
    append_lut(
        make_user_lut(
            adjustment.size, adjustment.domain_min, adjustment.domain_max,
            adjustment.table, adjustment.input_color_space,
            adjustment.output_color_space),
        strength);
  }
  return luts;
}

bool CompiledUserLut::ready() const {
  return payload_supported(
             lut.size, lut.table, lut.input_color_space,
             lut.output_color_space) &&
         std::ranges::all_of(
             working_to_input.values,
             [](double value) { return std::isfinite(value); }) &&
         std::ranges::all_of(
             output_to_working.values,
             [](double value) { return std::isfinite(value); }) &&
         strength > 0.0F && strength <= 1.0F &&
         input_transfer != TransferFunction::Unknown &&
         output_transfer != TransferFunction::Unknown &&
         lut.cache_identity != 0U && cache_identity != 0U;
}

bool CompiledUserLutStageProgram::ready() const {
  return active() && luts.size() == requested_count && cache_identity != 0U &&
         std::ranges::all_of(luts, [](const auto& lut) { return lut.ready(); });
}

bool user_lut_state_supports_compiled_execution(
    const ColorAdjustmentState& state) {
  if (state.lut_enabled &&
      std::clamp(state.lut_strength / 100.0, 0.0, 1.0) > 1.0e-6 &&
      !payload_supported(
          state.lut_size, state.lut_table, state.lut_input_color_space,
          state.lut_output_color_space)) {
    return false;
  }
  return std::ranges::all_of(
      state.lut_stack, [](const LutAdjustment& lut) {
        const auto active = lut.enabled &&
            std::clamp(lut.strength / 100.0, 0.0, 1.0) > 1.0e-6;
        return !active || payload_supported(
                              lut.size, lut.table, lut.input_color_space,
                              lut.output_color_space);
      });
}

CompiledUserLutStageProgram compile_user_lut_stage_program(
    const ColorAdjustmentState& state) {
  const auto context = make_pipeline_color_context(state);
  auto program = CompiledUserLutStageProgram{
      .luts = compile_user_lut_runtimes(
          state, context.working_space, context.chromatic_adaptation_method),
      .requested_count = requested_count(state),
  };
  if (program.luts.size() != program.requested_count || !program.active()) {
    return program;
  }
  auto identity = hash_append(
      kFnvOffset, static_cast<std::uint64_t>(program.requested_count));
  for (const auto& lut : program.luts) {
    identity = hash_append(identity, lut.cache_identity);
  }
  program.cache_identity = identity == 0U ? 1U : identity;
  return program;
}

ColorPixel apply_compiled_user_lut_stage_program(
    const CompiledUserLutStageProgram& program,
    const ColorPixel& pixel) {
  return program.ready() ? apply_user_luts_to_pixel(pixel, program.luts) : pixel;
}

}  // namespace photo_editor::pipeline
