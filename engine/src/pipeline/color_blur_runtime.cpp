#include "color_blur_runtime.h"

#include "color_adjustment_scheduler.h"

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
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

[[nodiscard]] std::array<int, 3> gaussian_approximation_radii(double sigma) {
  if (sigma <= 0.0) {
    return {0, 0, 0};
  }

  constexpr int kPassCount = 3;
  const auto ideal_width =
      std::sqrt((12.0 * sigma * sigma / static_cast<double>(kPassCount)) + 1.0);
  auto lower_width = static_cast<int>(std::floor(ideal_width));
  if ((lower_width % 2) == 0) {
    --lower_width;
  }
  lower_width = std::max(1, lower_width);
  const auto upper_width = lower_width + 2;
  const auto lower_count = std::clamp(
      static_cast<int>(std::round(
          (12.0 * sigma * sigma -
           static_cast<double>(kPassCount * lower_width * lower_width) -
           static_cast<double>(4 * kPassCount * lower_width) -
           static_cast<double>(3 * kPassCount)) /
          static_cast<double>(-4 * lower_width - 4))),
      0,
      kPassCount);

  std::array<int, 3> radii{};
  for (int pass = 0; pass < kPassCount; ++pass) {
    const auto width = pass < lower_count ? lower_width : upper_width;
    radii[static_cast<std::size_t>(pass)] = std::max(0, (width - 1) / 2);
  }
  return radii;
}

}  // namespace

void resize_blur_buffer(std::vector<float>* buffer, std::size_t size) {
  if (buffer != nullptr && buffer->size() != size) {
    buffer->resize(size);
  }
}

void box_blur_into(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    std::vector<float>* horizontal,
    std::vector<float>* output,
    CancellationToken cancellation) {
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0 ||
      horizontal == nullptr || output == nullptr ||
      horizontal->size() != input.size() || output->size() != input.size() ||
      cancellation.cancelled()) {
    return;
  }

  const auto kernel_width = radius * 2 + 1;
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    double sum = 0.0;
    for (int sample = -radius; sample <= radius; ++sample) {
      const auto clamped_x = std::clamp(sample, 0, width - 1);
      sum += input[static_cast<std::size_t>(y * width + clamped_x)];
    }

    for (int x = 0; x < width; ++x) {
      (*horizontal)[static_cast<std::size_t>(y * width + x)] =
          static_cast<float>(sum / kernel_width);
      const auto remove_x = std::clamp(x - radius, 0, width - 1);
      const auto add_x = std::clamp(x + radius + 1, 0, width - 1);
      sum += input[static_cast<std::size_t>(y * width + add_x)] -
             input[static_cast<std::size_t>(y * width + remove_x)];
    }
  });
  if (cancellation.cancelled()) {
    return;
  }

  parallel_for(width, [&](int x) {
    if (cancellation.cancelled()) {
      return;
    }
    double sum = 0.0;
    for (int sample = -radius; sample <= radius; ++sample) {
      const auto clamped_y = std::clamp(sample, 0, height - 1);
      sum += (*horizontal)[static_cast<std::size_t>(clamped_y * width + x)];
    }

    for (int y = 0; y < height; ++y) {
      (*output)[static_cast<std::size_t>(y * width + x)] =
          static_cast<float>(sum / kernel_width);
      const auto remove_y = std::clamp(y - radius, 0, height - 1);
      const auto add_y = std::clamp(y + radius + 1, 0, height - 1);
      sum += (*horizontal)[static_cast<std::size_t>(add_y * width + x)] -
             (*horizontal)[static_cast<std::size_t>(remove_y * width + x)];
    }
  });
}

std::vector<float> box_blur(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    CancellationToken cancellation) {
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0 ||
      cancellation.cancelled()) {
    return input;
  }

  std::vector<float> horizontal(input.size(), 0.0F);
  std::vector<float> output(input.size(), 0.0F);
  box_blur_into(input, width, height, radius, &horizontal, &output, cancellation);
  return cancellation.cancelled() ? input : output;
}

void gaussian_approximate_blur_into(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    std::vector<float>* output,
    BlurWorkspace* workspace,
    CancellationToken cancellation) {
  if (output == nullptr) {
    return;
  }
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0 ||
      cancellation.cancelled()) {
    *output = input;
    return;
  }

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = metal::shared_metal_pipeline_backend();
  if (metal.available()) {
    auto metal_output =
        metal.gaussian_approximate_blur(input, width, height, radius);
    *output = cancellation.cancelled() ? input : std::move(metal_output);
    return;
  }
#endif

  const auto radii = gaussian_approximation_radii(static_cast<double>(radius));
  if (std::ranges::none_of(radii, [](int pass_radius) { return pass_radius > 0; })) {
    *output = input;
    return;
  }

  BlurWorkspace local_workspace;
  auto& active_workspace = workspace != nullptr ? *workspace : local_workspace;
  resize_blur_buffer(&active_workspace.horizontal, input.size());
  resize_blur_buffer(&active_workspace.scratch, input.size());
  resize_blur_buffer(output, input.size());

  const auto* source = &input;
  auto* target = output;
  for (const auto pass_radius : radii) {
    if (pass_radius <= 0) {
      continue;
    }
    box_blur_into(
        *source,
        width,
        height,
        pass_radius,
        &active_workspace.horizontal,
        target,
        cancellation);
    if (cancellation.cancelled()) {
      *output = input;
      return;
    }
    source = target;
    target = target == output ? &active_workspace.scratch : output;
  }
  if (source != output) {
    output->swap(active_workspace.scratch);
  }
}

std::vector<float> gaussian_approximate_blur(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    CancellationToken cancellation) {
  std::vector<float> output;
  BlurWorkspace workspace;
  gaussian_approximate_blur_into(
      input,
      width,
      height,
      radius,
      &output,
      &workspace,
      cancellation);
  return output;
}

void guided_luminance_blur_into(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    double epsilon,
    std::vector<float>* output,
    BlurWorkspace* workspace,
    CancellationToken cancellation) {
  if (output == nullptr) {
    return;
  }
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0 ||
      cancellation.cancelled()) {
    *output = input;
    return;
  }

  BlurWorkspace local_workspace;
  auto& active_workspace = workspace != nullptr ? *workspace : local_workspace;
  resize_blur_buffer(&active_workspace.horizontal, input.size());
  resize_blur_buffer(output, input.size());

  std::vector<float> mean(input.size(), 0.0F);
  std::vector<float> correlation(input.size(), 0.0F);
  std::vector<float> mean_correlation(input.size(), 0.0F);
  std::vector<float> coefficient_a(input.size(), 0.0F);
  std::vector<float> coefficient_b(input.size(), 0.0F);
  std::vector<float> mean_a(input.size(), 0.0F);
  std::vector<float> mean_b(input.size(), 0.0F);

  const auto blur_to = [&](const std::vector<float>& source, std::vector<float>* target) {
    box_blur_into(
        source,
        width,
        height,
        radius,
        &active_workspace.horizontal,
        target,
        cancellation);
  };

  blur_to(input, &mean);
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }
  parallel_for(static_cast<int>(input.size()), [&](int index) {
    const auto value = static_cast<double>(input[static_cast<std::size_t>(index)]);
    correlation[static_cast<std::size_t>(index)] =
        static_cast<float>(value * value);
  });
  blur_to(correlation, &mean_correlation);
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }

  const auto safe_epsilon = std::max(1.0e-8, epsilon);
  parallel_for(static_cast<int>(input.size()), [&](int index) {
    const auto sample_index = static_cast<std::size_t>(index);
    const auto local_mean = static_cast<double>(mean[sample_index]);
    const auto variance = std::max(
        0.0,
        static_cast<double>(mean_correlation[sample_index]) -
            local_mean * local_mean);
    const auto a = variance / (variance + safe_epsilon);
    coefficient_a[sample_index] = static_cast<float>(a);
    coefficient_b[sample_index] =
        static_cast<float>(local_mean * (1.0 - a));
  });
  blur_to(coefficient_a, &mean_a);
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }
  blur_to(coefficient_b, &mean_b);
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }

  parallel_for(static_cast<int>(input.size()), [&](int index) {
    const auto sample_index = static_cast<std::size_t>(index);
    const auto filtered =
        static_cast<double>(mean_a[sample_index]) *
            static_cast<double>(input[sample_index]) +
        static_cast<double>(mean_b[sample_index]);
    (*output)[sample_index] = static_cast<float>(std::max(0.0, filtered));
  });
}

}  // namespace photo_editor::pipeline
