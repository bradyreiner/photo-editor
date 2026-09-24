#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <cstddef>
#include <vector>

namespace photo_editor::pipeline {

struct BlurWorkspace final {
  std::vector<float> horizontal;
  std::vector<float> scratch;
};

void resize_blur_buffer(
    std::vector<float>* buffer,
    std::size_t size);

void box_blur_into(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    std::vector<float>* horizontal,
    std::vector<float>* output,
    CancellationToken cancellation = {});

[[nodiscard]] std::vector<float> box_blur(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    CancellationToken cancellation = {});

void gaussian_approximate_blur_into(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    std::vector<float>* output,
    BlurWorkspace* workspace,
    CancellationToken cancellation = {});

[[nodiscard]] std::vector<float> gaussian_approximate_blur(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    CancellationToken cancellation = {});

void guided_luminance_blur_into(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    double epsilon,
    std::vector<float>* output,
    BlurWorkspace* workspace,
    CancellationToken cancellation = {});

}  // namespace photo_editor::pipeline
