#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/output_rendering.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using photo_editor::pipeline::Aces2OutputLut3D;
using photo_editor::pipeline::Aces2OutputLutQuality;
using photo_editor::pipeline::DisplayTarget;
using photo_editor::pipeline::RgbColorSpace;

struct Target final {
  std::string id;
  std::string semantic_id;
  DisplayTarget display_target;
};

struct OutputHashes final {
  std::uint64_t table = 0U;
  std::uint64_t pixels = 0U;
};

struct Request final {
  std::string_view phase;
  const Target* target = nullptr;
  Aces2OutputLutQuality quality = Aces2OutputLutQuality::Final;
};

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hash_u32(std::uint64_t& hash, std::uint32_t value) {
  for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
    hash ^= static_cast<std::uint8_t>(value >> shift);
    hash *= kFnvPrime;
  }
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) {
  for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
    hash ^= static_cast<std::uint8_t>(value >> shift);
    hash *= kFnvPrime;
  }
}

[[nodiscard]] std::uint64_t hash_table(const Aces2OutputLut3D& lut) {
  auto hash = kFnvOffset;
  for (const auto& sample : lut.table) {
    for (const auto channel : sample) {
      hash_u32(hash, std::bit_cast<std::uint32_t>(channel));
    }
  }
  return hash;
}

[[nodiscard]] std::uint64_t hash_pixels(
    const Aces2OutputLut3D& lut,
    const std::vector<std::array<double, 3>>& inputs) {
  auto hash = kFnvOffset;
  for (const auto& input : inputs) {
    const auto output = photo_editor::pipeline::aces2_output_lut_apply(
        lut,
        input);
    for (const auto channel : output) {
      hash_u64(hash, std::bit_cast<std::uint64_t>(channel));
    }
  }
  return hash;
}

[[nodiscard]] std::string hash_text(std::uint64_t hash) {
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

[[nodiscard]] const char* quality_name(Aces2OutputLutQuality quality) {
  return quality == Aces2OutputLutQuality::Interactive ? "interactive"
                                                       : "final";
}

[[nodiscard]] std::vector<std::array<double, 3>> make_pixels() {
  std::vector<std::array<double, 3>> pixels;
  pixels.reserve(256U);
  for (std::size_t index = 0U; index < 256U; ++index) {
    const auto x = static_cast<double>(index) / 255.0;
    const auto y = static_cast<double>((index * 73U) % 256U) / 255.0;
    const auto z = static_cast<double>((index * 151U) % 256U) / 255.0;
    const auto headroom = std::exp2(5.0 * x) - 1.0;
    pixels.push_back({
        headroom * (0.03 + 0.97 * y),
        headroom * (0.02 + 0.72 * (1.0 - y)),
        headroom * (0.01 + 0.54 * z),
    });
  }
  return pixels;
}

[[nodiscard]] long long peak_rss_bytes() {
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return -1;
  }
#if defined(__APPLE__)
  return static_cast<long long>(usage.ru_maxrss);
#else
  return static_cast<long long>(usage.ru_maxrss) * 1024LL;
#endif
}

void print_cache_snapshot(std::string_view phase) {
  const auto stats =
      photo_editor::pipeline::aces2_output_lut_cache_stats();
  std::cout << "{\"type\":\"cache\",\"phase\":\"" << phase
            << "\",\"requests\":" << stats.requests
            << ",\"hits\":" << stats.hits
            << ",\"misses\":" << stats.misses
            << ",\"builds\":" << stats.builds
            << ",\"evictions\":" << stats.evictions
            << ",\"entries\":" << stats.entries
            << ",\"bytes\":" << stats.bytes
            << ",\"maximum_bytes\":" << stats.maximum_bytes << "}\n";
}

void reset_output_caches() {
  photo_editor::pipeline::clear_aces2_output_lut_cache_for_testing();
  photo_editor::pipeline::clear_aces2_output_processor_cache_for_testing();
}

[[nodiscard]] bool run_request(
    const Request& request,
    const RgbColorSpace& working_space,
    const std::vector<std::array<double, 3>>& pixels,
    std::map<std::string, OutputHashes>& expected_hashes,
    std::vector<double>* compile_times_ms = nullptr) {
  const auto compile_start = Clock::now();
  const auto lut = photo_editor::pipeline::compile_aces2_output_lut(
      working_space,
      request.target->display_target,
      photo_editor::pipeline::aces2_output_lut_settings(request.quality));
  const auto compile_ms = std::chrono::duration<double, std::milli>(
                              Clock::now() - compile_start)
                              .count();
  if (compile_times_ms != nullptr) {
    compile_times_ms->push_back(compile_ms);
  }
  if (lut == nullptr) {
    std::cerr << "LUT request failed: target=" << request.target->id
              << " quality=" << quality_name(request.quality) << '\n';
    return false;
  }

  const auto table_hash_start = Clock::now();
  const auto table_hash_value = hash_table(*lut);
  const auto table_hash_ms = std::chrono::duration<double, std::milli>(
                                 Clock::now() - table_hash_start)
                                 .count();
  const auto pixel_hash_start = Clock::now();
  const auto pixel_hash_value = hash_pixels(*lut, pixels);
  const auto pixel_hash_ms = std::chrono::duration<double, std::milli>(
                                 Clock::now() - pixel_hash_start)
                                 .count();
  const auto hashes = OutputHashes{
      .table = table_hash_value,
      .pixels = pixel_hash_value,
  };
  const auto semantic_key = request.target->semantic_id + ":" +
                            quality_name(request.quality);
  const auto [seen, inserted] =
      expected_hashes.emplace(semantic_key, hashes);
  if (!inserted && (seen->second.table != hashes.table ||
                    seen->second.pixels != hashes.pixels)) {
    std::cerr << "deterministic output changed for semantic target "
              << semantic_key << '\n';
    return false;
  }

  std::cout << "{\"type\":\"request\",\"phase\":\""
            << request.phase << "\",\"target_id\":\""
            << request.target->id << "\",\"semantic_id\":\""
            << request.target->semantic_id << "\",\"quality\":\""
            << quality_name(request.quality) << "\",\"compile_ms\":"
            << std::setprecision(12) << compile_ms
            << ",\"table_hash_ms\":" << table_hash_ms
            << ",\"pixel_hash_ms\":" << pixel_hash_ms
            << ",\"table_hash\":\"" << hash_text(hashes.table)
            << "\",\"pixels_hash\":\"" << hash_text(hashes.pixels)
            << "\",\"lut_bytes\":" << lut->byte_size() << "}\n";
  const auto cache_label = request.target->id + "_" +
                           quality_name(request.quality);
  print_cache_snapshot(cache_label);
  return true;
}

[[nodiscard]] bool run_sequence(
    const std::vector<Request>& requests,
    const RgbColorSpace& working_space,
    const std::vector<std::array<double, 3>>& pixels,
    std::map<std::string, OutputHashes>& expected_hashes,
    std::vector<double>* compile_times_ms = nullptr) {
  for (const auto& request : requests) {
    if (!run_request(
            request,
            working_space,
            pixels,
            expected_hashes,
            compile_times_ms)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool run_warm_lookup_batches(
    const Target& target,
    const RgbColorSpace& working_space,
    const std::vector<std::array<double, 3>>& pixels,
    const std::map<std::string, OutputHashes>& expected_hashes,
    int batch_count,
    int requests_per_batch) {
  const auto quality = Aces2OutputLutQuality::Final;
  const auto semantic_key = target.semantic_id + ":" + quality_name(quality);
  const auto expected = expected_hashes.find(semantic_key);
  if (expected == expected_hashes.end()) {
    std::cerr << "missing exact output baseline for warm key "
              << semantic_key << '\n';
    return false;
  }

  auto expected_lut = photo_editor::pipeline::compile_aces2_output_lut(
      working_space,
      target.display_target,
      photo_editor::pipeline::aces2_output_lut_settings(quality));
  if (expected_lut == nullptr || expected_lut->cache_key.empty()) {
    std::cerr << "could not acquire expected warm LUT identity for "
              << semantic_key << '\n';
    return false;
  }
  const auto expected_cache_key = expected_lut->cache_key;
  const auto expected_table_hash = hash_table(*expected_lut);
  const auto expected_pixel_hash = hash_pixels(*expected_lut, pixels);
  if (expected_table_hash != expected->second.table ||
      expected_pixel_hash != expected->second.pixels) {
    std::cerr << "warm LUT differs from the earlier exact output for "
              << semantic_key << '\n';
    return false;
  }

  std::vector<double> batch_average_ns;
  batch_average_ns.reserve(static_cast<std::size_t>(batch_count));
  for (int batch = 0; batch < batch_count; ++batch) {
    const auto before =
        photo_editor::pipeline::aces2_output_lut_cache_stats();
    auto pointer_matches = std::uint64_t{0U};
    auto key_matches = std::uint64_t{0U};
    const auto start = Clock::now();
    for (int request = 0; request < requests_per_batch; ++request) {
      const auto lut = photo_editor::pipeline::compile_aces2_output_lut(
          working_space,
          target.display_target,
          photo_editor::pipeline::aces2_output_lut_settings(quality));
      if (lut == nullptr) {
        continue;
      }
      pointer_matches +=
          static_cast<std::uint64_t>(lut.get() == expected_lut.get());
      key_matches += static_cast<std::uint64_t>(
          lut->cache_key == expected_cache_key);
    }
    const auto elapsed_ns = std::chrono::duration<double, std::nano>(
                                Clock::now() - start)
                                .count();
    const auto after =
        photo_editor::pipeline::aces2_output_lut_cache_stats();
    const auto table_hash_after = hash_table(*expected_lut);
    const auto pixel_hash_after = hash_pixels(*expected_lut, pixels);
    const auto expected_count =
        static_cast<std::uint64_t>(requests_per_batch);
    const auto hits_delta = after.hits - before.hits;
    const auto requests_delta = after.requests - before.requests;
    const auto hashes_match =
        table_hash_after == expected_table_hash &&
        pixel_hash_after == expected_pixel_hash &&
        table_hash_after == expected->second.table &&
        pixel_hash_after == expected->second.pixels;
    std::cout << "{\"type\":\"warm_batch\",\"target_id\":\""
              << target.id << "\",\"semantic_key\":\"" << semantic_key
              << "\",\"batch\":" << batch
              << ",\"requests\":" << requests_per_batch
              << ",\"elapsed_ns\":" << std::setprecision(12) << elapsed_ns
              << ",\"amortized_ns_per_request\":"
              << elapsed_ns / static_cast<double>(requests_per_batch)
              << ",\"cache_requests_delta\":" << requests_delta
              << ",\"cache_hits_delta\":" << hits_delta
              << ",\"pointer_matches\":" << pointer_matches
              << ",\"cache_key_matches\":" << key_matches
              << ",\"table_hash\":\""
              << hash_text(table_hash_after)
              << "\",\"pixels_hash\":\""
              << hash_text(pixel_hash_after)
              << "\",\"hashes_match\":"
              << (hashes_match ? "true" : "false") << "}\n";
    const auto all_requests_matched =
        pointer_matches == expected_count && key_matches == expected_count &&
        requests_delta == expected_count && hits_delta == expected_count;
    if (!all_requests_matched || !hashes_match) {
      std::cerr << "warm lookup batch violated cache identity, hit, or exact-output guard: batch="
                << batch << " semantic=" << semantic_key << '\n';
      return false;
    }
    batch_average_ns.push_back(
        elapsed_ns / static_cast<double>(requests_per_batch));
    print_cache_snapshot(
        "warm_batch_" + std::to_string(batch));
  }

  auto sorted = batch_average_ns;
  std::sort(sorted.begin(), sorted.end());
  const auto total_requests =
      static_cast<std::uint64_t>(batch_count) *
      static_cast<std::uint64_t>(requests_per_batch);
  std::cout << "{\"type\":\"warm_batch_summary\",\"semantic_key\":\""
            << semantic_key << "\",\"batches\":" << batch_count
            << ",\"requests_per_batch\":" << requests_per_batch
            << ",\"total_requests\":" << total_requests
            << ",\"batch_average_p50_ns_per_request\":"
            << sorted[sorted.size() / 2U]
            << ",\"batch_average_p95_ns_per_request\":"
            << sorted.back() << ",\"batch_hits_delta_total\":"
            << total_requests
            << ",\"expected_table_hash\":\""
            << hash_text(expected_table_hash)
            << "\",\"expected_pixels_hash\":\""
            << hash_text(expected_pixel_hash) << "\"}\n";
  return true;
}

void print_phase_summary(
    std::string_view phase,
    const std::vector<double>& compile_times_ms) {
  auto sorted = compile_times_ms;
  std::sort(sorted.begin(), sorted.end());
  const auto percentile = [&sorted](double fraction) {
    if (sorted.empty()) {
      return 0.0;
    }
    const auto rank = static_cast<std::size_t>(std::ceil(
        fraction * static_cast<double>(sorted.size())));
    const auto index = std::min(
        sorted.size() - 1U,
        std::max<std::size_t>(1U, rank) - 1U);
    return sorted[index];
  };
  const auto total_compile_ms = std::accumulate(
      compile_times_ms.cbegin(),
      compile_times_ms.cend(),
      0.0);
  std::cout << "{\"type\":\"phase_summary\",\"phase\":\"" << phase
            << "\",\"requests\":" << compile_times_ms.size()
            << ",\"compile_total_ms\":" << std::setprecision(12)
            << total_compile_ms
            << ",\"compile_p50_ms\":" << percentile(0.50)
            << ",\"compile_p95_ms\":" << percentile(0.95) << "}\n";
}

}  // namespace

int main() {
  if (!photo_editor::pipeline::aces2_official_output_transform_available()) {
    std::cerr << "official ACES 2 OCIO transform unavailable; skipping\n";
    return 77;
  }

  using photo_editor::pipeline::DisplayEotf;
  using photo_editor::pipeline::DisplayMasteringMode;
  using photo_editor::pipeline::DisplaySurround;
  using photo_editor::pipeline::Aces2OutputLutQuality;
  const auto config = photo_editor::pipeline::default_color_pipeline_config();
  const auto working_space = config.working_profile.color_space;
  auto preview = Target{
      .id = "preview_srgb",
      .semantic_id = "srgb",
      .display_target = config.preview_display_target,
  };
  auto preview_name_alias = preview;
  preview_name_alias.id = "export_srgb_alias";
  preview_name_alias.display_target.target_name =
      "Export sRGB (same target values)";

  const auto p3_profile =
      photo_editor::pipeline::output_color_profile_from_name("Display P3");
  auto p3 = Target{
      .id = "preview_p3",
      .semantic_id = "display_p3_sdr",
      .display_target = photo_editor::pipeline::make_sdr_display_target(
          p3_profile),
  };
  auto hdr = Target{
      .id = "export_rec2020_pq_1000",
      .semantic_id = "rec2020_pq_1000",
      .display_target = DisplayTarget{
          .target_name = "Rec.2020 PQ 1000 nits",
          .color_space = photo_editor::pipeline::make_rec2020_color_space(),
          .eotf = DisplayEotf::Pq,
          .peak_luminance_nits = 1000.0,
          .black_luminance_nits = 0.0,
          .reference_white_nits = 203.0,
          .surround_luminance_nits = 5.0,
          .surround = DisplaySurround::Dim,
          .mastering_mode = DisplayMasteringMode::HdrDisplayReferred,
      },
  };

  for (const auto* target : {&preview, &preview_name_alias, &p3, &hdr}) {
    if (!photo_editor::pipeline::display_target_valid(
            target->display_target)) {
      std::cerr << "invalid test display target: " << target->id << '\n';
      return EXIT_FAILURE;
    }
  }

  const auto pixels = make_pixels();
  std::map<std::string, OutputHashes> expected_hashes;
  std::cout << "{\"type\":\"environment\",\"official_aces2\":true"
            << ",\"working_space\":\"" << working_space.name
            << "\",\"pixels\":" << pixels.size()
            << ",\"peak_rss_before_bytes\":" << peak_rss_bytes()
            << "}\n";

  reset_output_caches();
  print_cache_snapshot("cold_start");
  std::vector<double> prewarm_times;
  const std::vector<Request> cache_fit = {
      {"default_prewarm", &preview, Aces2OutputLutQuality::Interactive},
      {"default_prewarm", &preview, Aces2OutputLutQuality::Final},
      {"second_display_touch", &p3, Aces2OutputLutQuality::Interactive},
  };
  if (!run_sequence(
          cache_fit,
          working_space,
          pixels,
          expected_hashes,
          &prewarm_times)) {
    return EXIT_FAILURE;
  }
  print_cache_snapshot("after_prewarm_and_second_display_touch");
  print_phase_summary("cold_prewarm_and_second_display_touch", prewarm_times);
  const std::vector<Request> cache_fit_revisit = {
      {"three_key_revisit", &p3, Aces2OutputLutQuality::Interactive},
      {"three_key_revisit", &preview, Aces2OutputLutQuality::Interactive},
      {"three_key_revisit", &preview, Aces2OutputLutQuality::Final},
  };
  std::vector<double> cache_fit_revisit_times;
  for (int cycle = 0; cycle < 5; ++cycle) {
    if (!run_sequence(
            cache_fit_revisit,
            working_space,
            pixels,
            expected_hashes,
            &cache_fit_revisit_times)) {
      return EXIT_FAILURE;
    }
  }
  print_cache_snapshot("after_three_key_revisits");
  print_phase_summary("prewarm_three_key_revisit", cache_fit_revisit_times);

  reset_output_caches();
  print_cache_snapshot("single_target_cold_start");
  std::vector<double> single_target_times;
  const std::vector<Request> single_target = {
      {"single_target_cold", &preview, Aces2OutputLutQuality::Final},
  };
  if (!run_sequence(
          single_target,
          working_space,
          pixels,
          expected_hashes,
          &single_target_times)) {
    return EXIT_FAILURE;
  }
  std::vector<double> single_target_warm_times;
  const std::vector<Request> single_target_warm = {
      {"single_target_warm", &preview, Aces2OutputLutQuality::Final},
  };
  for (int iteration = 0; iteration < 7; ++iteration) {
    if (!run_sequence(
            single_target_warm,
            working_space,
            pixels,
            expected_hashes,
            &single_target_warm_times)) {
      return EXIT_FAILURE;
    }
  }
  single_target_times.insert(
      single_target_times.end(),
      single_target_warm_times.begin(),
      single_target_warm_times.end());
  print_cache_snapshot("after_single_target_warm");
  print_phase_summary("single_target_cold_and_warm", single_target_times);
  print_phase_summary("single_target_warm_only", single_target_warm_times);
  if (!run_warm_lookup_batches(
          preview,
          working_space,
          pixels,
          expected_hashes,
          7,
          100)) {
    return EXIT_FAILURE;
  }

  reset_output_caches();
  print_cache_snapshot("multi_profile_cold_start");
  std::vector<double> multi_profile_times;
  const std::vector<Request> multi_profile = {
      {"mixed_profile_quality", &preview, Aces2OutputLutQuality::Interactive},
      {"mixed_profile_quality", &preview, Aces2OutputLutQuality::Final},
      {"mixed_profile_quality", &p3, Aces2OutputLutQuality::Interactive},
      {"mixed_profile_quality", &hdr, Aces2OutputLutQuality::Final},
      {"mixed_profile_quality", &p3, Aces2OutputLutQuality::Final},
      {"mixed_profile_quality", &hdr, Aces2OutputLutQuality::Interactive},
      {"mixed_profile_quality", &preview, Aces2OutputLutQuality::Final},
      {"mixed_profile_quality", &p3, Aces2OutputLutQuality::Interactive},
  };
  if (!run_sequence(
          multi_profile,
          working_space,
          pixels,
          expected_hashes,
          &multi_profile_times)) {
    return EXIT_FAILURE;
  }
  print_cache_snapshot("after_multi_profile_quality");
  print_phase_summary("multi_profile_quality_churn", multi_profile_times);

  reset_output_caches();
  print_cache_snapshot("name_alias_cold_start");
  std::vector<double> alias_times;
  const std::vector<Request> name_aliases = {
      {"same_numeric_target_name_alias", &preview, Aces2OutputLutQuality::Final},
      {"same_numeric_target_name_alias",
       &preview_name_alias,
       Aces2OutputLutQuality::Final},
      {"same_numeric_target_name_alias", &preview, Aces2OutputLutQuality::Final},
      {"same_numeric_target_name_alias", &preview_name_alias, Aces2OutputLutQuality::Final},
  };
  if (!run_sequence(
          name_aliases,
          working_space,
          pixels,
          expected_hashes,
          &alias_times)) {
    return EXIT_FAILURE;
  }
  print_cache_snapshot("after_name_alias_sequence");
  print_phase_summary("same_numeric_target_name_alias", alias_times);

  std::cout << "{\"type\":\"summary\",\"peak_rss_bytes\":"
            << peak_rss_bytes() << ",\"semantic_outputs_checked\":"
            << expected_hashes.size() << ",\"exact_repeats_passed\":true}\n";
  return EXIT_SUCCESS;
}
