#include "color_detail_runtime.h"

#include "color_adjustment_scheduler.h"
#include "color_blur_runtime.h"
#include "color_math_runtime.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

template <typename Work>
void parallel_for(int count, Work&& work) {
  run_color_adjustment_parallel_for(
      count,
      std::function<void(int)>(std::forward<Work>(work)));
}

using StageClock = std::chrono::steady_clock;

[[nodiscard]] std::int64_t elapsed_stage_ms(StageClock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      StageClock::now() - start).count();
}

}  // namespace

[[nodiscard]] bool apply_spatial_detail_effects(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() !=
          static_cast<std::size_t>(width) *
              static_cast<std::size_t>(height) ||
      cancellation.cancelled()) {
    return false;
  }
  const auto texture_strength =
      clamp_signed_percent(state.texture) / 100.0;
  const auto clarity_strength =
      clamp_signed_percent(state.clarity) / 100.0;
  const auto dehaze_strength =
      clamp_signed_percent(state.dehaze) / 100.0;
  const auto has_texture_effect =
      std::abs(texture_strength) > 1e-6;
  const auto has_clarity_effect =
      std::abs(clarity_strength) > 1e-6;
  const auto has_dehaze_effect =
      std::abs(dehaze_strength) > 1e-6;
  if (!has_texture_effect && !has_clarity_effect &&
      !has_dehaze_effect) {
    return true;
  }

  const auto spatial_timer = StageClock::now();
  const auto safe_width =
      static_cast<std::size_t>(width);
  const auto color_context =
      make_pipeline_color_context(state);
  const auto final_output_headroom =
      sanitized_output_headroom(state);
  std::vector<float> luminance_buffer(
      static_cast<std::size_t>(width * height),
      0.0F);
  const auto spatial_luminance_timer =
      StageClock::now();
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset =
        static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index =
          row_offset + static_cast<std::size_t>(x);
      luminance_buffer[index] = static_cast<float>(
          luminance_for_pixel(
              (*pixels)[index],
              color_context));
    }
  });
  if (cancellation.cancelled()) {
    return false;
  }
  if (telemetry != nullptr) {
    telemetry->spatial_luminance_ms +=
        elapsed_stage_ms(spatial_luminance_timer);
  }

  BlurWorkspace spatial_blur_workspace;
  const auto blur_spatial_luminance_into =
      [&](int radius,
          double epsilon,
          std::vector<float>* output) {
        if (removal_quality ==
            RemovalQuality::Final) {
          guided_luminance_blur_into(
              luminance_buffer,
              width,
              height,
              radius,
              epsilon,
              output,
              &spatial_blur_workspace,
              cancellation);
          return;
        }
        resize_blur_buffer(
            &spatial_blur_workspace.horizontal,
            luminance_buffer.size());
        resize_blur_buffer(
            output,
            luminance_buffer.size());
        box_blur_into(
            luminance_buffer,
            width,
            height,
            radius,
            &spatial_blur_workspace.horizontal,
            output,
            cancellation);
      };
  std::vector<float> texture_blur;
  if (has_texture_effect) {
    const auto timer = StageClock::now();
    blur_spatial_luminance_into(
        2, 0.00045, &texture_blur);
    if (cancellation.cancelled()) {
      return false;
    }
    if (telemetry != nullptr) {
      telemetry->spatial_texture_blur_ms +=
          elapsed_stage_ms(timer);
    }
  }
  std::vector<float> clarity_blur;
  if (has_clarity_effect) {
    const auto timer = StageClock::now();
    blur_spatial_luminance_into(
        8, 0.0018, &clarity_blur);
    if (cancellation.cancelled()) {
      return false;
    }
    if (telemetry != nullptr) {
      telemetry->spatial_clarity_blur_ms +=
          elapsed_stage_ms(timer);
    }
  }
  std::vector<float> haze_blur;
  if (has_dehaze_effect) {
    const auto timer = StageClock::now();
    blur_spatial_luminance_into(
        20, 0.0065, &haze_blur);
    if (cancellation.cancelled()) {
      return false;
    }
    if (telemetry != nullptr) {
      telemetry->spatial_haze_blur_ms +=
          elapsed_stage_ms(timer);
    }
  }

  const auto spatial_apply_timer =
      StageClock::now();
  const auto texture_gain =
      texture_strength >= 0.0 ? 1.30 : 0.48;
  const auto clarity_gain =
      clarity_strength >= 0.0 ? 1.08 : 0.46;
  const auto dehaze_gain =
      dehaze_strength >= 0.0 ? 0.95 : 0.36;
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset =
        static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index =
          row_offset + static_cast<std::size_t>(x);
      const auto luminance =
          static_cast<double>(
              luminance_buffer[index]);
      auto local_activity = 0.0;
      if (x > 0) {
        local_activity = std::max(
            local_activity,
            std::abs(
                luminance -
                static_cast<double>(
                    luminance_buffer[index - 1])));
      }
      if (x + 1 < width) {
        local_activity = std::max(
            local_activity,
            std::abs(
                luminance -
                static_cast<double>(
                    luminance_buffer[index + 1])));
      }
      if (y > 0) {
        local_activity = std::max(
            local_activity,
            std::abs(
                luminance -
                static_cast<double>(
                    luminance_buffer[
                        index - safe_width])));
      }
      if (y + 1 < height) {
        local_activity = std::max(
            local_activity,
            std::abs(
                luminance -
                static_cast<double>(
                    luminance_buffer[
                        index + safe_width])));
      }
      const auto midtone_weight =
          bell_weight(luminance, 0.5, 0.42);
      const auto shadow_guard =
          smoothstep(0.025, 0.12, luminance);
      const auto highlight_headroom =
          1.0 - smoothstep(0.74, 0.98, luminance);
      const auto tone_guard =
          shadow_guard * highlight_headroom;
      auto target_luminance = luminance;
      if (has_texture_effect) {
        const auto texture_signal =
            static_cast<double>(
                luminance_buffer[index] -
                texture_blur[index]);
        const auto broad_edge_signal =
            has_clarity_effect
                ? static_cast<double>(
                      luminance_buffer[index] -
                      clarity_blur[index])
                : local_activity;
        const auto broad_edge_guard =
            1.0 -
            smoothstep(
                0.08,
                0.36,
                std::abs(broad_edge_signal)) *
                0.72;
        target_luminance +=
            texture_strength * texture_signal *
            (0.46 + midtone_weight * 0.70) *
            broad_edge_guard * tone_guard *
            texture_gain;
      }
      if (has_clarity_effect) {
        const auto clarity_signal =
            static_cast<double>(
                luminance_buffer[index] -
                clarity_blur[index]);
        const auto edge_limit =
            1.0 -
            smoothstep(
                0.24,
                0.58,
                std::abs(clarity_signal)) *
                0.28;
        const auto structure_gate =
            smoothstep(
                0.003,
                0.035,
                local_activity);
        target_luminance +=
            clarity_strength * clarity_signal *
            (0.36 + midtone_weight * 0.82) *
            tone_guard * edge_limit *
            structure_gate * clarity_gain;
      }
      if (has_dehaze_effect) {
        const auto haze_signal = std::clamp(
            static_cast<double>(
                luminance_buffer[index] -
                haze_blur[index]),
            -0.18,
            0.18);
        const auto haze_activity_gate =
            smoothstep(
                0.0015,
                0.018,
                local_activity);
        const auto haze_edge_guard =
            1.0 -
            smoothstep(
                0.11,
                0.18,
                std::abs(haze_signal)) *
                0.95;
        target_luminance +=
            dehaze_strength * haze_signal *
            (0.52 + midtone_weight * 0.78) *
            highlight_headroom *
            haze_activity_gate *
            haze_edge_guard * dehaze_gain;
        const auto haze_shadow_weight =
            1.0 -
            smoothstep(0.08, 0.20, luminance);
        const auto haze_structure_weight =
            smoothstep(
                0.008,
                0.065,
                std::abs(haze_signal)) *
            haze_activity_gate * haze_edge_guard;
        if (dehaze_strength > 0.0) {
          target_luminance *= std::max(
              0.0,
              1.0 -
                  dehaze_strength *
                      haze_shadow_weight *
                      haze_structure_weight *
                      0.52);
          target_luminance *= std::max(
              0.0,
              1.0 -
                  dehaze_strength *
                      haze_shadow_weight *
                      haze_activity_gate *
                      haze_edge_guard * 0.035);
        } else {
          target_luminance +=
              (-dehaze_strength) *
              haze_shadow_weight *
              haze_structure_weight * 0.075;
        }
      }
      (*pixels)[index] =
          scale_pixel_to_luminance_with_headroom(
              (*pixels)[index],
              target_luminance,
              final_output_headroom,
              color_context);
    }
  });
  if (cancellation.cancelled()) {
    return false;
  }
  if (telemetry != nullptr) {
    telemetry->spatial_apply_ms +=
        elapsed_stage_ms(spatial_apply_timer);
    telemetry->spatial_ms +=
        elapsed_stage_ms(spatial_timer);
  }
  return true;
}

void apply_fine_detail_effects(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() !=
          static_cast<std::size_t>(width) *
              static_cast<std::size_t>(height) ||
      cancellation.cancelled()) {
    return;
  }
  const auto safe_width =
      static_cast<std::size_t>(width);
  const auto color_context =
      make_pipeline_color_context(state);
  const auto sharpen_strength =
      clamp_signed_percent(state.sharpening) / 100.0;
  const auto sharpen_radius =
      std::clamp(state.sharpen_radius, 0.5, 3.0);
  const auto sharpen_detail =
      std::clamp(state.sharpen_detail, 0.0, 100.0) / 100.0;
  const auto sharpen_mask_strength =
      clamp_signed_percent(state.sharpen_masking) / 100.0;
  const auto luma_nr_strength =
      clamp_signed_percent(state.luminance_nr) / 100.0;
  const auto color_nr_strength =
      clamp_signed_percent(state.color_nr) / 100.0;
  const auto has_detail_effects =
      std::abs(sharpen_strength) > 1e-6 ||
      std::abs(luma_nr_strength) > 1e-6 ||
      std::abs(color_nr_strength) > 1e-6;

  // ── Detail pass: sharpening and noise reduction in OKLab space ──
  // Runs after both the fast (LUT) and slow (per-pixel) tone/color paths.
  if (has_detail_effects) {
    const auto detail_timer = StageClock::now();
    if (cancellation.cancelled()) {
      return;
    }
    // Convert to OKLab buffers for perceptual processing
    const auto pixel_count = static_cast<std::size_t>(width * height);
    std::vector<float> lightness_buf(pixel_count);
    std::vector<float> chroma_a_buf(pixel_count);
    std::vector<float> chroma_b_buf(pixel_count);
    BlurWorkspace detail_blur_workspace;

    const auto detail_oklab_timer = StageClock::now();
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto ok = linear_rgb_to_oklab((*pixels)[index], color_context);
        lightness_buf[index] = static_cast<float>(ok.lightness);
        chroma_a_buf[index] = static_cast<float>(ok.a);
        chroma_b_buf[index] = static_cast<float>(ok.b);
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
    if (telemetry != nullptr) {
      telemetry->detail_oklab_ms += elapsed_stage_ms(detail_oklab_timer);
    }

    // Sharpening: unsharp mask on lightness with edge-aware masking. Radius
    // controls the blur scale; Detail increases fine-edge gain without
    // changing the user's amount.
    if (std::abs(sharpen_strength) > 1e-6) {
      const auto detail_sharpen_timer = StageClock::now();
      const auto sharp_radius = std::max(1, static_cast<int>(std::lround(sharpen_radius)));
      const auto edge_radius = std::max(sharp_radius + 2, static_cast<int>(std::lround(sharpen_radius * 3.0)));
      const auto sharpen_gain = 1.9 + sharpen_detail * 1.15;
      std::vector<float> sharp_blur;
      gaussian_approximate_blur_into(
          lightness_buf,
          width,
          height,
          sharp_radius,
          &sharp_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }
      std::vector<float> edge_blur;
      if (sharpen_mask_strength > 1e-6) {
        gaussian_approximate_blur_into(
            lightness_buf,
            width,
            height,
            edge_radius,
            &edge_blur,
            &detail_blur_workspace,
            cancellation);
        if (cancellation.cancelled()) {
          return;
        }
      }

      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        const auto row_offset = static_cast<std::size_t>(y) * safe_width;
        for (int x = 0; x < width; ++x) {
          const auto index = row_offset + static_cast<std::size_t>(x);
          const auto detail_signal = lightness_buf[index] - sharp_blur[index];
          auto mask_gate = 1.0;
          if (sharpen_mask_strength > 1e-6) {
            const auto edge_strength = std::abs(lightness_buf[index] - edge_blur[index]);
            mask_gate = smoothstep(
                0.0015,
                0.012 + 0.06 * sharpen_mask_strength,
                static_cast<double>(edge_strength));
          }
          lightness_buf[index] += static_cast<float>(
              static_cast<double>(detail_signal) * sharpen_strength * sharpen_gain * mask_gate);
        }
      });
      if (cancellation.cancelled()) {
        return;
      }
      if (telemetry != nullptr) {
        telemetry->detail_sharpen_ms += elapsed_stage_ms(detail_sharpen_timer);
      }
    }

    // Luminance noise reduction: edge-preserving blur on lightness.
    // Uses a box blur at radius 2 and blends toward it proportional to
    // strength, but preserves edges by reducing blend where local contrast
    // is high.
    if (std::abs(luma_nr_strength) > 1e-6) {
      const auto detail_luminance_nr_timer = StageClock::now();
      std::vector<float> nr_blur;
      gaussian_approximate_blur_into(
          lightness_buf,
          width,
          height,
          2,
          &nr_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }
      std::vector<float> nr_edge_blur;
      gaussian_approximate_blur_into(
          lightness_buf,
          width,
          height,
          5,
          &nr_edge_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }

      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        const auto row_offset = static_cast<std::size_t>(y) * safe_width;
        for (int x = 0; x < width; ++x) {
          const auto index = row_offset + static_cast<std::size_t>(x);
          const auto edge = std::abs(lightness_buf[index] - nr_edge_blur[index]);
          const auto edge_preserve = 1.0 - smoothstep(0.005, 0.04, static_cast<double>(edge));
          const auto blend = luma_nr_strength * 0.85 * edge_preserve;
          lightness_buf[index] = static_cast<float>(
              static_cast<double>(lightness_buf[index]) * (1.0 - blend) +
              static_cast<double>(nr_blur[index]) * blend);
        }
      });
      if (cancellation.cancelled()) {
        return;
      }
      if (telemetry != nullptr) {
        telemetry->detail_luminance_nr_ms += elapsed_stage_ms(detail_luminance_nr_timer);
      }
    }

    // Color noise reduction: edge-preserving blur on chrominance (a, b).
    // Chrominance noise is typically stronger than luminance noise, so
    // a wider radius and stronger blend are used.
    if (std::abs(color_nr_strength) > 1e-6) {
      const auto detail_color_nr_timer = StageClock::now();
      std::vector<float> a_blur;
      gaussian_approximate_blur_into(
          chroma_a_buf,
          width,
          height,
          3,
          &a_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }
      std::vector<float> b_blur;
      gaussian_approximate_blur_into(
          chroma_b_buf,
          width,
          height,
          3,
          &b_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }

      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        const auto row_offset = static_cast<std::size_t>(y) * safe_width;
        const auto blend = color_nr_strength * 0.90;
        for (int x = 0; x < width; ++x) {
          const auto index = row_offset + static_cast<std::size_t>(x);
          chroma_a_buf[index] = static_cast<float>(
              static_cast<double>(chroma_a_buf[index]) * (1.0 - blend) +
              static_cast<double>(a_blur[index]) * blend);
          chroma_b_buf[index] = static_cast<float>(
              static_cast<double>(chroma_b_buf[index]) * (1.0 - blend) +
              static_cast<double>(b_blur[index]) * blend);
        }
      });
      if (cancellation.cancelled()) {
        return;
      }
      if (telemetry != nullptr) {
        telemetry->detail_color_nr_ms += elapsed_stage_ms(detail_color_nr_timer);
      }
    }

    // Convert back from OKLab to linear RGB
    const auto detail_output_timer = StageClock::now();
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto lightness =
            static_cast<double>(lightness_buf[index]);
        const auto a = static_cast<double>(chroma_a_buf[index]);
        const auto b = static_cast<double>(chroma_b_buf[index]);
        (*pixels)[index] = oklab_to_linear_rgb(
            OklabColor{
                .lightness = lightness,
                .a = a,
                .b = b,
            },
            (*pixels)[index].alpha,
            color_context);
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
    if (telemetry != nullptr) {
      telemetry->detail_output_ms += elapsed_stage_ms(detail_output_timer);
      telemetry->detail_ms += elapsed_stage_ms(detail_timer);
    }
  }

}



}  // namespace photo_editor::pipeline
