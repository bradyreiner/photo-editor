#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace photo_editor::pipeline::metal {

namespace {

constexpr double kPi = std::numbers::pi_v<double>;
static_assert(sizeof(ColorPixel) == sizeof(float) * 4);

struct ColorAdjustmentUniforms {
  float exposure_gain;
  float wb_red;
  float wb_green;
  float wb_blue;
  float contrast;
  float highlights;
  float shadows;
  float whites;
  float blacks;
  float curve_shadows;
  float curve_darks;
  float curve_lights;
  float curve_highlights;
  float saturation_strength;
  float vibrance_strength;
  float dehaze_sat_contrib;
  float hue_adjustment;
  float saturation_adjustment;
  float luminance_adjustment;
  int lut_size;
  int width;
  int height;
};

struct MaskUniforms {
  int has_linear_mask;
  float linear_exposure;
  float linear_temperature;
  float linear_feather;
  float linear_position;
  float linear_rotation_rad;
  int has_radial_mask;
  float radial_exposure;
  float radial_temperature;
  float radial_feather;
  float radial_center_x;
  float radial_center_y;
  float radial_radius_x;
  float radial_radius_y;
  float radial_rotation_rad;
  float radial_wb_red;
  float radial_wb_green;
  float radial_wb_blue;
  float linear_wb_red;
  float linear_wb_green;
  float linear_wb_blue;
  int width;
  int height;
};

struct BlurUniforms {
  int width;
  int height;
  int radius;
};

struct DetailUniforms {
  float sharpen_strength;
  float sharpen_mask_strength;
  float luma_nr_strength;
  float color_nr_strength;
  int width;
  int height;
};

struct SpatialToneUniforms {
  float texture_strength;
  float clarity_strength;
  float dehaze_strength;
  int width;
  int height;
};

struct PostColorUniforms {
  float saturation_strength;
  float vibrance_strength;
  float dehaze_strength;
  float grading_hue[3];
  float grading_sat[3];
  float grading_lum[3];
  float mixer_hue[8];
  float mixer_sat[8];
  float mixer_lum[8];
  int use_master_curve;
  int use_red_curve;
  int use_green_curve;
  int use_blue_curve;
  int curve_lut_size;
  int width;
  int height;
};

struct PreToneUniforms {
  float exposure_gain;
  float global_wb_red;
  float global_wb_green;
  float global_wb_blue;
  float contrast;
  float highlights;
  float shadows;
  float whites;
  float blacks;
  float shoulder_threshold;
  int use_parametric_curve;
  int curve_lut_size;
  int has_linear_mask;
  float linear_exposure;
  float linear_feather;
  float linear_position;
  float linear_rotation_rad;
  float linear_wb_red;
  float linear_wb_green;
  float linear_wb_blue;
  int has_radial_mask;
  float radial_exposure;
  float radial_feather;
  float radial_center_x;
  float radial_center_y;
  float radial_radius_x;
  float radial_radius_y;
  float radial_rotation_rad;
  float radial_wb_red;
  float radial_wb_green;
  float radial_wb_blue;
  int has_local_mask;
  float local_exposure;
  float local_wb_red;
  float local_wb_green;
  float local_wb_blue;
  int width;
  int height;
};

std::array<int, 3> gaussian_approximation_radii(double sigma) {
  if (sigma <= 0.0) {
    return {0, 0, 0};
  }

  constexpr int kPassCount = 3;
  const auto ideal_width = std::sqrt((12.0 * sigma * sigma / static_cast<double>(kPassCount)) + 1.0);
  auto lower_width = static_cast<int>(std::floor(ideal_width));
  if ((lower_width % 2) == 0) {
    --lower_width;
  }
  lower_width = std::max(1, lower_width);
  const auto upper_width = lower_width + 2;
  const auto lower_count = std::clamp(static_cast<int>(std::round(
      (12.0 * sigma * sigma -
       static_cast<double>(kPassCount * lower_width * lower_width) -
       static_cast<double>(4 * kPassCount * lower_width) -
       static_cast<double>(3 * kPassCount)) /
      static_cast<double>(-4 * lower_width - 4))), 0, kPassCount);

  std::array<int, 3> radii{};
  for (int pass = 0; pass < kPassCount; ++pass) {
    const auto width = pass < lower_count ? lower_width : upper_width;
    radii[static_cast<std::size_t>(pass)] = std::max(0, (width - 1) / 2);
  }
  return radii;
}

std::array<float, 3> wb_multipliers(double temperature, double tint) {
  const auto temperature_to_kelvin = [](double slider_value) {
    const auto clamped = std::clamp(slider_value, -100.0, 100.0);
    if (clamped >= 0.0) {
      return std::lerp(5500.0, 2000.0, clamped / 100.0);
    }
    return std::lerp(5500.0, 12000.0, -clamped / 100.0);
  };

  const auto chromaticity_from_kelvin = [](double kelvin) {
    const auto safe_kelvin = std::clamp(kelvin, 2000.0, 12000.0);
    double x = 0.3127;
    double y = 0.3290;
    if (safe_kelvin < 4000.0) {
      x = -0.2661239e9 / std::pow(safe_kelvin, 3.0) -
          0.2343589e6 / std::pow(safe_kelvin, 2.0) +
          0.8776956e3 / safe_kelvin + 0.179910;
      if (safe_kelvin < 2222.0) {
        y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
      } else {
        y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
      }
    } else {
      if (safe_kelvin <= 7000.0) {
        x = -4.6070e9 / std::pow(safe_kelvin, 3.0) +
            2.9678e6 / std::pow(safe_kelvin, 2.0) +
            0.09911e3 / safe_kelvin + 0.244063;
      } else {
        x = -2.0064e9 / std::pow(safe_kelvin, 3.0) +
            1.9018e6 / std::pow(safe_kelvin, 2.0) +
            0.24748e3 / safe_kelvin + 0.237040;
      }
      y = -3.0 * x * x + 2.87 * x - 0.275;
    }
    return std::array<double, 2>{x, y};
  };

  const auto white_rgb_from_kelvin = [&](double kelvin) {
    const auto chromaticity = chromaticity_from_kelvin(kelvin);
    const auto x = chromaticity[0];
    const auto y = std::max(1e-6, chromaticity[1]);
    const auto X = x / y;
    const auto Y = 1.0;
    const auto Z = (1.0 - x - y) / y;
    return std::array<double, 3>{
        std::max(1e-6, 3.2406 * X - 1.5372 * Y - 0.4986 * Z),
        std::max(1e-6, -0.9689 * X + 1.8758 * Y + 0.0415 * Z),
        std::max(1e-6, 0.0557 * X - 0.2040 * Y + 1.0570 * Z),
    };
  };

  const auto reference_white = white_rgb_from_kelvin(5500.0);
  const auto target_white = white_rgb_from_kelvin(temperature_to_kelvin(temperature));
  const auto norm_n = std::clamp(tint, -100.0, 100.0) / 100.0;
  const auto r = (target_white[0] / reference_white[0]) * std::pow(2.0, norm_n * 0.08);
  const auto g = (target_white[1] / reference_white[1]) * std::pow(2.0, -norm_n * 0.24);
  const auto b = (target_white[2] / reference_white[2]) * std::pow(2.0, norm_n * 0.08);
  const auto norm = r * 0.2126 + g * 0.7152 + b * 0.0722;
  if (norm <= 1e-6) return {1.0f, 1.0f, 1.0f};
  return {static_cast<float>(r / norm), static_cast<float>(g / norm), static_cast<float>(b / norm)};
}

MaskUniforms make_mask_uniforms(const ColorAdjustmentState& state, int width, int height) {
  const auto linear_wb = wb_multipliers(state.linear_mask_temperature, 0.0);
  const auto radial_wb = wb_multipliers(state.radial_mask_temperature, 0.0);

  MaskUniforms uniforms{};
  uniforms.has_linear_mask = state.linear_mask_enabled ? 1 : 0;
  uniforms.linear_exposure = static_cast<float>(state.linear_mask_exposure);
  uniforms.linear_temperature = static_cast<float>(state.linear_mask_temperature);
  uniforms.linear_feather = static_cast<float>(state.linear_mask_feather);
  uniforms.linear_position = static_cast<float>(state.linear_mask_position);
  uniforms.linear_rotation_rad = static_cast<float>(state.linear_mask_rotation * kPi / 180.0);
  uniforms.linear_wb_red = linear_wb[0];
  uniforms.linear_wb_green = linear_wb[1];
  uniforms.linear_wb_blue = linear_wb[2];
  uniforms.has_radial_mask = state.radial_mask_enabled ? 1 : 0;
  uniforms.radial_exposure = static_cast<float>(state.radial_mask_exposure);
  uniforms.radial_temperature = static_cast<float>(state.radial_mask_temperature);
  uniforms.radial_feather = static_cast<float>(state.radial_mask_feather);
  uniforms.radial_center_x = static_cast<float>(state.radial_mask_center_x);
  uniforms.radial_center_y = static_cast<float>(state.radial_mask_center_y);
  uniforms.radial_radius_x = static_cast<float>(state.radial_mask_radius_x);
  uniforms.radial_radius_y = static_cast<float>(state.radial_mask_radius_y);
  uniforms.radial_rotation_rad = static_cast<float>(state.radial_mask_rotation * kPi / 180.0);
  uniforms.radial_wb_red = radial_wb[0];
  uniforms.radial_wb_green = radial_wb[1];
  uniforms.radial_wb_blue = radial_wb[2];
  uniforms.width = width;
  uniforms.height = height;
  return uniforms;
}

void copy_pixels_to_float_buffer(float* destination, const std::vector<ColorPixel>& pixels) {
  std::memcpy(destination, pixels.data(), pixels.size() * sizeof(ColorPixel));
}

void copy_pixels_from_float_buffer(std::vector<ColorPixel>* pixels, const float* source) {
  std::memcpy(pixels->data(), source, pixels->size() * sizeof(ColorPixel));
}

}  // namespace

struct MetalPipelineBackend::Impl {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> command_queue = nil;
  id<MTLComputePipelineState> lut_pipeline = nil;
  id<MTLComputePipelineState> mask_pipeline = nil;
  id<MTLComputePipelineState> blur_horizontal_pipeline = nil;
  id<MTLComputePipelineState> blur_vertical_pipeline = nil;
  id<MTLComputePipelineState> extract_oklab_pipeline = nil;
  id<MTLComputePipelineState> extract_luminance_pipeline = nil;
  id<MTLComputePipelineState> sharpen_pipeline = nil;
  id<MTLComputePipelineState> luma_nr_pipeline = nil;
  id<MTLComputePipelineState> color_nr_pipeline = nil;
  id<MTLComputePipelineState> compose_detail_pipeline = nil;
  id<MTLComputePipelineState> spatial_tone_pipeline = nil;
  id<MTLComputePipelineState> post_color_pipeline = nil;
  id<MTLComputePipelineState> pre_tone_input_pipeline = nil;
  id<MTLComputePipelineState> pre_tone_output_pipeline = nil;
  bool is_available = false;

  Impl() {
    @autoreleasepool {
      device = MTLCreateSystemDefaultDevice();
      if (!device) return;

      command_queue = [device newCommandQueue];
      if (!command_queue) return;

      NSError* error = nil;
      NSString* shader_source = load_shader_source();
      if (!shader_source) return;

      id<MTLLibrary> library = [device newLibraryWithSource:shader_source
                                                    options:nil
                                                      error:&error];
      if (!library || error) return;

      id<MTLFunction> lut_function = [library newFunctionWithName:@"apply_color_lut"];
      id<MTLFunction> mask_function = [library newFunctionWithName:@"apply_masks"];
      id<MTLFunction> blur_horizontal_function = [library newFunctionWithName:@"box_blur_horizontal"];
      id<MTLFunction> blur_vertical_function = [library newFunctionWithName:@"box_blur_vertical"];
      id<MTLFunction> extract_oklab_function = [library newFunctionWithName:@"extract_oklab"];
      id<MTLFunction> extract_luminance_function = [library newFunctionWithName:@"extract_luminance"];
      id<MTLFunction> sharpen_function = [library newFunctionWithName:@"apply_sharpen"];
      id<MTLFunction> luma_nr_function = [library newFunctionWithName:@"apply_luma_nr"];
      id<MTLFunction> color_nr_function = [library newFunctionWithName:@"apply_color_nr"];
      id<MTLFunction> compose_detail_function = [library newFunctionWithName:@"compose_detail_pixels"];
      id<MTLFunction> spatial_tone_function = [library newFunctionWithName:@"apply_spatial_tone"];
      id<MTLFunction> post_color_function = [library newFunctionWithName:@"apply_post_color"];
      id<MTLFunction> pre_tone_input_function = [library newFunctionWithName:@"compute_pre_tone_inputs"];
      id<MTLFunction> pre_tone_output_function = [library newFunctionWithName:@"apply_pre_tone_output"];
      if (!lut_function || !mask_function || !blur_horizontal_function || !blur_vertical_function ||
          !extract_oklab_function || !extract_luminance_function || !sharpen_function ||
          !luma_nr_function || !color_nr_function || !compose_detail_function ||
          !spatial_tone_function || !post_color_function ||
          !pre_tone_input_function || !pre_tone_output_function) {
        return;
      }

      lut_pipeline = [device newComputePipelineStateWithFunction:lut_function error:&error];
      if (!lut_pipeline || error) return;

      mask_pipeline = [device newComputePipelineStateWithFunction:mask_function error:&error];
      if (!mask_pipeline || error) return;

      blur_horizontal_pipeline =
          [device newComputePipelineStateWithFunction:blur_horizontal_function error:&error];
      if (!blur_horizontal_pipeline || error) return;

      blur_vertical_pipeline =
          [device newComputePipelineStateWithFunction:blur_vertical_function error:&error];
      if (!blur_vertical_pipeline || error) return;

      extract_oklab_pipeline =
          [device newComputePipelineStateWithFunction:extract_oklab_function error:&error];
      if (!extract_oklab_pipeline || error) return;

      extract_luminance_pipeline =
          [device newComputePipelineStateWithFunction:extract_luminance_function error:&error];
      if (!extract_luminance_pipeline || error) return;

      sharpen_pipeline = [device newComputePipelineStateWithFunction:sharpen_function error:&error];
      if (!sharpen_pipeline || error) return;

      luma_nr_pipeline = [device newComputePipelineStateWithFunction:luma_nr_function error:&error];
      if (!luma_nr_pipeline || error) return;

      color_nr_pipeline = [device newComputePipelineStateWithFunction:color_nr_function error:&error];
      if (!color_nr_pipeline || error) return;

      compose_detail_pipeline =
          [device newComputePipelineStateWithFunction:compose_detail_function error:&error];
      if (!compose_detail_pipeline || error) return;

      spatial_tone_pipeline =
          [device newComputePipelineStateWithFunction:spatial_tone_function error:&error];
      if (!spatial_tone_pipeline || error) return;

      post_color_pipeline =
          [device newComputePipelineStateWithFunction:post_color_function error:&error];
      if (!post_color_pipeline || error) return;

      pre_tone_input_pipeline =
          [device newComputePipelineStateWithFunction:pre_tone_input_function error:&error];
      if (!pre_tone_input_pipeline || error) return;

      pre_tone_output_pipeline =
          [device newComputePipelineStateWithFunction:pre_tone_output_function error:&error];
      if (!pre_tone_output_pipeline || error) return;

      is_available = true;
    }
  }

  NSString* load_shader_source() {
    // Look for the shader relative to the executable
    NSBundle* bundle = [NSBundle mainBundle];
    NSString* path = [bundle pathForResource:@"color_adjustments" ofType:@"metal"];
    if (path) {
      return [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    }

    // Development/test fallback: load the checked-in source next to this file.
    NSString* backend_source = [NSString stringWithUTF8String:__FILE__];
    NSString* source_tree_path =
        [[backend_source stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"color_adjustments.metal"];
    NSString* source_tree_shader =
        [NSString stringWithContentsOfFile:[source_tree_path stringByStandardizingPath]
                                  encoding:NSUTF8StringEncoding
                                     error:nil];
    if (source_tree_shader) {
      return source_tree_shader;
    }

    // Fallback: embed the shader source inline for development builds
    return @R"(
#include <metal_stdlib>
using namespace metal;

struct ColorAdjustmentUniforms {
  float exposure_gain;
  float wb_red; float wb_green; float wb_blue;
  float contrast; float highlights; float shadows; float whites; float blacks;
  float curve_shadows; float curve_darks; float curve_lights; float curve_highlights;
  float saturation_strength; float vibrance_strength; float dehaze_sat_contrib;
  float hue_adjustment; float saturation_adjustment; float luminance_adjustment;
  int lut_size; int width; int height;
};

static float clamp_unit(float v) { return clamp(v, 0.0f, 1.0f); }

static float4 apply_lut_3d(float3 rgb, device const float4* lut, int lut_size) {
  float sm1 = float(lut_size - 1);
  float rf = clamp_unit(rgb.r) * sm1, gf = clamp_unit(rgb.g) * sm1, bf = clamp_unit(rgb.b) * sm1;
  int ri = min(int(rf), lut_size - 2), gi = min(int(gf), lut_size - 2), bi = min(int(bf), lut_size - 2);
  float fr = rf - float(ri), fg = gf - float(gi), fb = bf - float(bi);
  float4 c000 = lut[(ri*lut_size+gi)*lut_size+bi], c001 = lut[(ri*lut_size+gi)*lut_size+bi+1];
  float4 c010 = lut[(ri*lut_size+(gi+1))*lut_size+bi], c011 = lut[(ri*lut_size+(gi+1))*lut_size+bi+1];
  float4 c100 = lut[((ri+1)*lut_size+gi)*lut_size+bi], c101 = lut[((ri+1)*lut_size+gi)*lut_size+bi+1];
  float4 c110 = lut[((ri+1)*lut_size+(gi+1))*lut_size+bi], c111 = lut[((ri+1)*lut_size+(gi+1))*lut_size+bi+1];
  return mix(mix(mix(c000,c001,fb), mix(c010,c011,fb), fg), mix(mix(c100,c101,fb), mix(c110,c111,fb), fg), fr);
}

kernel void apply_color_lut(device float4* pixels [[buffer(0)]], device const float4* lut [[buffer(1)]],
    constant ColorAdjustmentUniforms& u [[buffer(2)]], uint gid [[thread_position_in_grid]]) {
  if (gid >= uint(u.width) * uint(u.height)) return;
  float4 p = pixels[gid];
  float4 r = apply_lut_3d(p.rgb, lut, u.lut_size);
  pixels[gid] = float4(r.rgb, p.a);
}

struct MaskUniforms {
  int has_linear_mask; float linear_exposure; float linear_temperature; float linear_feather;
  float linear_position; float linear_rotation_rad;
  int has_radial_mask; float radial_exposure; float radial_temperature; float radial_feather;
  float radial_center_x; float radial_center_y; float radial_radius_x; float radial_radius_y;
  float radial_rotation_rad;
  float radial_wb_red; float radial_wb_green; float radial_wb_blue;
  float linear_wb_red; float linear_wb_green; float linear_wb_blue;
  int width; int height;
};

struct BlurUniforms {
  int width; int height; int radius;
};

kernel void apply_masks(device float4* pixels [[buffer(0)]], constant MaskUniforms& u [[buffer(1)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uint(u.width) * uint(u.height)) return;
  int x = int(gid) % u.width, y = int(gid) / u.width;
  float nx = u.width > 1 ? float(x) / float(u.width - 1) : 0.5f;
  float ny = u.height > 1 ? float(y) / float(u.height - 1) : 0.5f;
  float4 p = pixels[gid];
  if (u.has_linear_mask) {
    float cr = cos(u.linear_rotation_rad), sr = sin(u.linear_rotation_rad);
    float axis = (nx - 0.5f) * cr + (ny - 0.5f) * sr + 0.5f;
    float w = clamp_unit((u.linear_position - axis) / max(0.05f, u.linear_feather) + 0.5f);
    if (w > 0.0f) { float g = pow(2.0f, u.linear_exposure * w);
      p.r *= (1.0f + (u.linear_wb_red - 1.0f) * w) * g;
      p.g *= (1.0f + (u.linear_wb_green - 1.0f) * w) * g;
      p.b *= (1.0f + (u.linear_wb_blue - 1.0f) * w) * g; }
  }
  if (u.has_radial_mask) {
    float cr = cos(u.radial_rotation_rad), sr = sin(u.radial_rotation_rad);
    float2 t = float2(nx, ny) - float2(u.radial_center_x, u.radial_center_y);
    float lx = t.x * cr + t.y * sr, ly = -t.x * sr + t.y * cr;
    float2 sr2 = max(float2(u.radial_radius_x, u.radial_radius_y), float2(0.05f));
    float d = sqrt(lx*lx/(sr2.x*sr2.x) + ly*ly/(sr2.y*sr2.y));
    float inner = max(0.0f, 1.0f - clamp(u.radial_feather, 0.05f, 1.0f));
    float w = 0.0f;
    if (d <= inner) w = 1.0f;
    else if (d < 1.0f) { float s = clamp_unit((d - inner) / (1.0f - inner)); w = 1.0f - s*s*(3.0f-2.0f*s); }
    if (w > 0.0f) { float g = pow(2.0f, u.radial_exposure * w);
      p.r *= (1.0f + (u.radial_wb_red - 1.0f) * w) * g;
      p.g *= (1.0f + (u.radial_wb_green - 1.0f) * w) * g;
      p.b *= (1.0f + (u.radial_wb_blue - 1.0f) * w) * g; }
  }
  pixels[gid] = max(p, float4(0.0f));
}

kernel void box_blur_horizontal(device const float* input [[buffer(0)]], device float* output [[buffer(1)]],
    constant BlurUniforms& u [[buffer(2)]], uint gid [[thread_position_in_grid]]) {
  if (gid >= uint(u.width) * uint(u.height)) return;
  int x = int(gid) % u.width, y = int(gid) / u.width;
  int kernel_width = u.radius * 2 + 1;
  float sum = 0.0f;
  for (int sample = -u.radius; sample <= u.radius; ++sample) {
    int clamped_x = clamp(x + sample, 0, u.width - 1);
    sum += input[y * u.width + clamped_x];
  }
  output[gid] = sum / float(kernel_width);
}

kernel void box_blur_vertical(device const float* input [[buffer(0)]], device float* output [[buffer(1)]],
    constant BlurUniforms& u [[buffer(2)]], uint gid [[thread_position_in_grid]]) {
  if (gid >= uint(u.width) * uint(u.height)) return;
  int x = int(gid) % u.width, y = int(gid) / u.width;
  int kernel_width = u.radius * 2 + 1;
  float sum = 0.0f;
  for (int sample = -u.radius; sample <= u.radius; ++sample) {
    int clamped_y = clamp(y + sample, 0, u.height - 1);
    sum += input[clamped_y * u.width + x];
  }
  output[gid] = sum / float(kernel_width);
}
)";
  }
};

MetalPipelineBackend::MetalPipelineBackend() : impl_(std::make_unique<Impl>()) {}
MetalPipelineBackend::~MetalPipelineBackend() = default;

bool MetalPipelineBackend::available() const { return impl_ && impl_->is_available; }

void MetalPipelineBackend::apply_color_lut(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorLut3D& lut) {
  if (!available() || !pixels || !lut.valid) return;
  const auto pixel_count = static_cast<std::size_t>(width * height);
  if (pixels->size() != pixel_count) return;

  @autoreleasepool {
    // Convert pixels to float4 buffer
    const auto buffer_size = pixel_count * sizeof(float) * 4;
    id<MTLBuffer> pixel_buffer = [impl_->device newBufferWithLength:buffer_size
                                                            options:MTLResourceStorageModeShared];
    auto* pixel_data = static_cast<float*>([pixel_buffer contents]);
    copy_pixels_to_float_buffer(pixel_data, *pixels);

    // Upload LUT
    const auto lut_buffer_size = lut.table.size() * sizeof(float) * 4;
    id<MTLBuffer> lut_buffer = [impl_->device newBufferWithLength:lut_buffer_size
                                                           options:MTLResourceStorageModeShared];
    auto* lut_data = static_cast<float*>([lut_buffer contents]);
    copy_pixels_to_float_buffer(lut_data, lut.table);

    ColorAdjustmentUniforms uniforms{};
    uniforms.lut_size = kLutSize;
    uniforms.width = width;
    uniforms.height = height;

    id<MTLCommandBuffer> command_buffer = [impl_->command_queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
    [encoder setComputePipelineState:impl_->lut_pipeline];
    [encoder setBuffer:pixel_buffer offset:0 atIndex:0];
    [encoder setBuffer:lut_buffer offset:0 atIndex:1];
    [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:2];

    NSUInteger thread_group_size = impl_->lut_pipeline.maxTotalThreadsPerThreadgroup;
    if (thread_group_size > 256) thread_group_size = 256;
    MTLSize grid_size = MTLSizeMake(pixel_count, 1, 1);
    MTLSize group_size = MTLSizeMake(thread_group_size, 1, 1);
    [encoder dispatchThreads:grid_size threadsPerThreadgroup:group_size];
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    // Read back results
    copy_pixels_from_float_buffer(pixels, pixel_data);
  }
}

void MetalPipelineBackend::apply_masks(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  if (!available() || !pixels) return;
  if (!state.linear_mask_enabled && !state.radial_mask_enabled) return;
  const auto pixel_count = static_cast<std::size_t>(width * height);
  if (pixels->size() != pixel_count) return;

  @autoreleasepool {
    const auto buffer_size = pixel_count * sizeof(float) * 4;
    id<MTLBuffer> pixel_buffer = [impl_->device newBufferWithLength:buffer_size
                                                            options:MTLResourceStorageModeShared];
    auto* pixel_data = static_cast<float*>([pixel_buffer contents]);
    copy_pixels_to_float_buffer(pixel_data, *pixels);

    const auto uniforms = make_mask_uniforms(state, width, height);

    id<MTLCommandBuffer> command_buffer = [impl_->command_queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
    [encoder setComputePipelineState:impl_->mask_pipeline];
    [encoder setBuffer:pixel_buffer offset:0 atIndex:0];
    [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:1];

    NSUInteger thread_group_size = impl_->mask_pipeline.maxTotalThreadsPerThreadgroup;
    if (thread_group_size > 256) thread_group_size = 256;
    MTLSize grid_size = MTLSizeMake(pixel_count, 1, 1);
    MTLSize group_size = MTLSizeMake(thread_group_size, 1, 1);
    [encoder dispatchThreads:grid_size threadsPerThreadgroup:group_size];
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    copy_pixels_from_float_buffer(pixels, pixel_data);
  }
}

void MetalPipelineBackend::apply_masks_and_color_lut(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const ColorLut3D& lut) {
  if (!available() || !pixels || !lut.valid) return;
  const auto pixel_count = static_cast<std::size_t>(width * height);
  if (pixels->size() != pixel_count) return;

  @autoreleasepool {
    const auto buffer_size = pixel_count * sizeof(float) * 4;
    id<MTLBuffer> pixel_buffer = [impl_->device newBufferWithLength:buffer_size
                                                            options:MTLResourceStorageModeShared];
    const auto lut_buffer_size = lut.table.size() * sizeof(float) * 4;
    id<MTLBuffer> lut_buffer = [impl_->device newBufferWithLength:lut_buffer_size
                                                          options:MTLResourceStorageModeShared];
    if (!pixel_buffer || !lut_buffer) return;

    auto* pixel_data = static_cast<float*>([pixel_buffer contents]);
    copy_pixels_to_float_buffer(pixel_data, *pixels);

    auto* lut_data = static_cast<float*>([lut_buffer contents]);
    copy_pixels_to_float_buffer(lut_data, lut.table);

    id<MTLCommandBuffer> command_buffer = [impl_->command_queue commandBuffer];
    if (!command_buffer) return;

    if (state.linear_mask_enabled || state.radial_mask_enabled) {
      const auto mask_uniforms = make_mask_uniforms(state, width, height);
      id<MTLComputeCommandEncoder> mask_encoder = [command_buffer computeCommandEncoder];
      [mask_encoder setComputePipelineState:impl_->mask_pipeline];
      [mask_encoder setBuffer:pixel_buffer offset:0 atIndex:0];
      [mask_encoder setBytes:&mask_uniforms length:sizeof(mask_uniforms) atIndex:1];

      NSUInteger thread_group_size = impl_->mask_pipeline.maxTotalThreadsPerThreadgroup;
      if (thread_group_size > 256) thread_group_size = 256;
      [mask_encoder dispatchThreads:MTLSizeMake(pixel_count, 1, 1)
               threadsPerThreadgroup:MTLSizeMake(thread_group_size, 1, 1)];
      [mask_encoder endEncoding];
    }

    ColorAdjustmentUniforms lut_uniforms{};
    lut_uniforms.lut_size = kLutSize;
    lut_uniforms.width = width;
    lut_uniforms.height = height;

    id<MTLComputeCommandEncoder> lut_encoder = [command_buffer computeCommandEncoder];
    [lut_encoder setComputePipelineState:impl_->lut_pipeline];
    [lut_encoder setBuffer:pixel_buffer offset:0 atIndex:0];
    [lut_encoder setBuffer:lut_buffer offset:0 atIndex:1];
    [lut_encoder setBytes:&lut_uniforms length:sizeof(lut_uniforms) atIndex:2];

    NSUInteger thread_group_size = impl_->lut_pipeline.maxTotalThreadsPerThreadgroup;
    if (thread_group_size > 256) thread_group_size = 256;
    [lut_encoder dispatchThreads:MTLSizeMake(pixel_count, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(thread_group_size, 1, 1)];
    [lut_encoder endEncoding];

    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    copy_pixels_from_float_buffer(pixels, pixel_data);
  }
}

std::vector<float> MetalPipelineBackend::gaussian_approximate_blur(
    const std::vector<float>& input,
    int width,
    int height,
    int radius) {
  if (!available() || input.empty() || width <= 0 || height <= 0 || radius <= 0) {
    return input;
  }

  const auto pixel_count = static_cast<std::size_t>(width * height);
  if (input.size() != pixel_count) {
    return input;
  }

  @autoreleasepool {
    const auto buffer_size = pixel_count * sizeof(float);
    id<MTLBuffer> source_buffer = [impl_->device newBufferWithLength:buffer_size
                                                             options:MTLResourceStorageModeShared];
    id<MTLBuffer> temp_buffer = [impl_->device newBufferWithLength:buffer_size
                                                           options:MTLResourceStorageModeShared];
    if (!source_buffer || !temp_buffer) {
      return input;
    }

    auto* source_data = static_cast<float*>([source_buffer contents]);
    std::copy(input.begin(), input.end(), source_data);

    id<MTLCommandBuffer> command_buffer = [impl_->command_queue commandBuffer];
    if (!command_buffer) {
      return input;
    }

    const auto dispatch_pass = [&](id<MTLComputePipelineState> pipeline,
                                   id<MTLBuffer> in_buffer,
                                   id<MTLBuffer> out_buffer,
                                   const BlurUniforms& uniforms) {
      id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
      [encoder setComputePipelineState:pipeline];
      [encoder setBuffer:in_buffer offset:0 atIndex:0];
      [encoder setBuffer:out_buffer offset:0 atIndex:1];
      [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:2];

      NSUInteger thread_group_size = pipeline.maxTotalThreadsPerThreadgroup;
      if (thread_group_size > 256) thread_group_size = 256;
      MTLSize grid_size = MTLSizeMake(pixel_count, 1, 1);
      MTLSize group_size = MTLSizeMake(thread_group_size, 1, 1);
      [encoder dispatchThreads:grid_size threadsPerThreadgroup:group_size];
      [encoder endEncoding];
    };

    id<MTLBuffer> current_input = source_buffer;
    id<MTLBuffer> current_output = temp_buffer;
    for (const auto pass_radius : gaussian_approximation_radii(static_cast<double>(radius))) {
      if (pass_radius <= 0) {
        continue;
      }

      const BlurUniforms uniforms{
          .width = width,
          .height = height,
          .radius = pass_radius,
      };
      dispatch_pass(impl_->blur_horizontal_pipeline, current_input, current_output, uniforms);
      std::swap(current_input, current_output);
      dispatch_pass(impl_->blur_vertical_pipeline, current_input, current_output, uniforms);
      std::swap(current_input, current_output);
    }
    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    std::vector<float> output(pixel_count, 0.0F);
    const auto* result_data = static_cast<const float*>([current_input contents]);
    std::copy(result_data, result_data + pixel_count, output.begin());
    return output;
  }
}

bool MetalPipelineBackend::apply_spatial_tone_effects(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    double texture_strength,
    double clarity_strength,
    double dehaze_strength) {
  if (!available() || pixels == nullptr || width <= 0 || height <= 0) {
    return false;
  }

  const auto pixel_count = static_cast<std::size_t>(width * height);
  if (pixels->size() != pixel_count) {
    return false;
  }

  if (std::abs(texture_strength) <= 1e-6 &&
      std::abs(clarity_strength) <= 1e-6 &&
      std::abs(dehaze_strength) <= 1e-6) {
    return true;
  }

  @autoreleasepool {
    const auto pixel_buffer_size = pixel_count * sizeof(float) * 4;
    const auto scalar_buffer_size = pixel_count * sizeof(float);
    id<MTLBuffer> pixel_buffer = [impl_->device newBufferWithLength:pixel_buffer_size
                                                            options:MTLResourceStorageModeShared];
    id<MTLBuffer> luminance_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                                options:MTLResourceStorageModeShared];
    id<MTLBuffer> texture_blur_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> clarity_blur_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> haze_blur_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                                options:MTLResourceStorageModeShared];
    id<MTLBuffer> scratch_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                              options:MTLResourceStorageModeShared];
    if (!pixel_buffer || !luminance_buffer || !texture_blur_buffer || !clarity_blur_buffer ||
        !haze_blur_buffer || !scratch_buffer) {
      return false;
    }

    auto* pixel_data = static_cast<float*>([pixel_buffer contents]);
    copy_pixels_to_float_buffer(pixel_data, *pixels);

    const SpatialToneUniforms uniforms{
        .texture_strength = static_cast<float>(texture_strength),
        .clarity_strength = static_cast<float>(clarity_strength),
        .dehaze_strength = static_cast<float>(dehaze_strength),
        .width = width,
        .height = height,
    };

    id<MTLCommandBuffer> command_buffer = [impl_->command_queue commandBuffer];
    if (!command_buffer) {
      return false;
    }

    const auto dispatch_pipeline = [&](id<MTLComputePipelineState> pipeline,
                                       const std::vector<id<MTLBuffer>>& buffers,
                                       int uniforms_index,
                                       const void* raw_uniforms,
                                       size_t uniforms_size) {
      id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
      [encoder setComputePipelineState:pipeline];
      for (std::size_t index = 0; index < buffers.size(); ++index) {
        [encoder setBuffer:buffers[index] offset:0 atIndex:index];
      }
      [encoder setBytes:raw_uniforms length:uniforms_size atIndex:static_cast<NSUInteger>(uniforms_index)];

      NSUInteger thread_group_size = pipeline.maxTotalThreadsPerThreadgroup;
      if (thread_group_size > 256) thread_group_size = 256;
      MTLSize grid_size = MTLSizeMake(pixel_count, 1, 1);
      MTLSize group_size = MTLSizeMake(thread_group_size, 1, 1);
      [encoder dispatchThreads:grid_size threadsPerThreadgroup:group_size];
      [encoder endEncoding];
    };

    const auto copy_buffer = [&](id<MTLBuffer> source, id<MTLBuffer> destination) {
      id<MTLBlitCommandEncoder> encoder = [command_buffer blitCommandEncoder];
      [encoder copyFromBuffer:source sourceOffset:0 toBuffer:destination destinationOffset:0 size:scalar_buffer_size];
      [encoder endEncoding];
    };

    const auto run_gaussian_blur = [&](id<MTLBuffer> source, id<MTLBuffer> output, int radius) {
      copy_buffer(source, output);
      if (radius <= 0) {
        return;
      }

      for (const auto pass_radius : gaussian_approximation_radii(static_cast<double>(radius))) {
        if (pass_radius <= 0) {
          continue;
        }

        const BlurUniforms blur_uniforms{
            .width = width,
            .height = height,
            .radius = pass_radius,
        };
        dispatch_pipeline(
            impl_->blur_horizontal_pipeline,
            {output, scratch_buffer},
            2,
            &blur_uniforms,
            sizeof(blur_uniforms));
        dispatch_pipeline(
            impl_->blur_vertical_pipeline,
            {scratch_buffer, output},
            2,
            &blur_uniforms,
            sizeof(blur_uniforms));
      }
    };

    dispatch_pipeline(
        impl_->extract_luminance_pipeline,
        {pixel_buffer, luminance_buffer},
        2,
        &uniforms,
        sizeof(uniforms));

    run_gaussian_blur(luminance_buffer, texture_blur_buffer, 2);
    run_gaussian_blur(luminance_buffer, clarity_blur_buffer, 8);
    run_gaussian_blur(luminance_buffer, haze_blur_buffer, 20);

    dispatch_pipeline(
        impl_->spatial_tone_pipeline,
        {pixel_buffer, luminance_buffer, texture_blur_buffer, clarity_blur_buffer, haze_blur_buffer},
        5,
        &uniforms,
        sizeof(uniforms));

    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    copy_pixels_from_float_buffer(pixels, pixel_data);
    return true;
  }
}

bool MetalPipelineBackend::apply_post_color_effects(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  if (!available() || pixels == nullptr || width <= 0 || height <= 0) {
    return false;
  }

  const auto pixel_count = static_cast<std::size_t>(width * height);
  if (pixels->size() != pixel_count) {
    return false;
  }

  constexpr int kCurveLutSize = 256;
  const auto build_identity_curve = [] {
    std::vector<float> lut(static_cast<std::size_t>(kCurveLutSize), 0.0F);
    for (int index = 0; index < kCurveLutSize; ++index) {
      lut[static_cast<std::size_t>(index)] =
          kCurveLutSize > 1 ? static_cast<float>(index) / static_cast<float>(kCurveLutSize - 1) : 0.0F;
    }
    return lut;
  };

  auto master_curve_lut = build_perceptual_curve_lut(state.master_curve_points, kCurveLutSize);
  auto red_curve_lut = build_perceptual_curve_lut(state.red_curve_points, kCurveLutSize);
  auto green_curve_lut = build_perceptual_curve_lut(state.green_curve_points, kCurveLutSize);
  auto blue_curve_lut = build_perceptual_curve_lut(state.blue_curve_points, kCurveLutSize);

  const auto use_master_curve = !master_curve_lut.empty();
  const auto use_red_curve = !red_curve_lut.empty();
  const auto use_green_curve = !green_curve_lut.empty();
  const auto use_blue_curve = !blue_curve_lut.empty();
  const auto has_per_channel_mixer = std::any_of(
      state.color_mixer_channels.begin(), state.color_mixer_channels.end(),
      [](const ColorMixerChannelAdjustment& channel) {
        return std::abs(channel.hue) > 1e-6 || std::abs(channel.saturation) > 1e-6 ||
               std::abs(channel.luminance) > 1e-6;
      });

  if (!use_master_curve && !use_red_curve && !use_green_curve && !use_blue_curve &&
      std::abs(state.saturation) <= 1e-6 && std::abs(state.vibrance) <= 1e-6 &&
      std::abs(state.grading_shadows_hue) <= 1e-6 && std::abs(state.grading_shadows_saturation) <= 1e-6 &&
      std::abs(state.grading_shadows_luminance) <= 1e-6 &&
      std::abs(state.grading_midtones_hue) <= 1e-6 && std::abs(state.grading_midtones_saturation) <= 1e-6 &&
      std::abs(state.grading_midtones_luminance) <= 1e-6 &&
      std::abs(state.grading_highlights_hue) <= 1e-6 && std::abs(state.grading_highlights_saturation) <= 1e-6 &&
      std::abs(state.grading_highlights_luminance) <= 1e-6 &&
      !has_per_channel_mixer &&
      (std::abs(state.hue_adjustment) <= 1e-6 || state.color_mixer_channel.empty()) &&
      std::abs(state.saturation_adjustment) <= 1e-6 &&
      std::abs(state.luminance_adjustment) <= 1e-6) {
    return true;
  }

  if (!use_master_curve) {
    master_curve_lut = build_identity_curve();
  }
  if (!use_red_curve) {
    red_curve_lut = build_identity_curve();
  }
  if (!use_green_curve) {
    green_curve_lut = build_identity_curve();
  }
  if (!use_blue_curve) {
    blue_curve_lut = build_identity_curve();
  }

  @autoreleasepool {
    const auto pixel_buffer_size = pixel_count * sizeof(float) * 4;
    const auto curve_buffer_size = static_cast<std::size_t>(kCurveLutSize) * sizeof(float);
    id<MTLBuffer> pixel_buffer = [impl_->device newBufferWithLength:pixel_buffer_size
                                                            options:MTLResourceStorageModeShared];
    id<MTLBuffer> master_curve_buffer = [impl_->device newBufferWithLength:curve_buffer_size
                                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> red_curve_buffer = [impl_->device newBufferWithLength:curve_buffer_size
                                                                options:MTLResourceStorageModeShared];
    id<MTLBuffer> green_curve_buffer = [impl_->device newBufferWithLength:curve_buffer_size
                                                                  options:MTLResourceStorageModeShared];
    id<MTLBuffer> blue_curve_buffer = [impl_->device newBufferWithLength:curve_buffer_size
                                                                 options:MTLResourceStorageModeShared];
    if (!pixel_buffer || !master_curve_buffer || !red_curve_buffer || !green_curve_buffer || !blue_curve_buffer) {
      return false;
    }

    auto* pixel_data = static_cast<float*>([pixel_buffer contents]);
    copy_pixels_to_float_buffer(pixel_data, *pixels);

    std::copy(master_curve_lut.begin(), master_curve_lut.end(), static_cast<float*>([master_curve_buffer contents]));
    std::copy(red_curve_lut.begin(), red_curve_lut.end(), static_cast<float*>([red_curve_buffer contents]));
    std::copy(green_curve_lut.begin(), green_curve_lut.end(), static_cast<float*>([green_curve_buffer contents]));
    std::copy(blue_curve_lut.begin(), blue_curve_lut.end(), static_cast<float*>([blue_curve_buffer contents]));

    PostColorUniforms uniforms{};
    uniforms.saturation_strength = static_cast<float>(clamp_signed_percent(state.saturation) / 100.0);
    uniforms.vibrance_strength = static_cast<float>(clamp_signed_percent(state.vibrance) / 100.0);
    uniforms.dehaze_strength = static_cast<float>(clamp_signed_percent(state.dehaze) / 100.0);
    uniforms.grading_hue[0] = static_cast<float>(state.grading_shadows_hue);
    uniforms.grading_hue[1] = static_cast<float>(state.grading_midtones_hue);
    uniforms.grading_hue[2] = static_cast<float>(state.grading_highlights_hue);
    uniforms.grading_sat[0] = static_cast<float>(state.grading_shadows_saturation);
    uniforms.grading_sat[1] = static_cast<float>(state.grading_midtones_saturation);
    uniforms.grading_sat[2] = static_cast<float>(state.grading_highlights_saturation);
    uniforms.grading_lum[0] = static_cast<float>(state.grading_shadows_luminance);
    uniforms.grading_lum[1] = static_cast<float>(state.grading_midtones_luminance);
    uniforms.grading_lum[2] = static_cast<float>(state.grading_highlights_luminance);
    for (int channel = 0; channel < kColorMixerChannelCount; ++channel) {
      auto mixer = state.color_mixer_channels[static_cast<std::size_t>(channel)];
      if (!has_per_channel_mixer &&
          state.color_mixer_channel == kColorMixerChannelNames[static_cast<std::size_t>(channel)] &&
          std::abs(mixer.hue) <= 1e-6 && std::abs(mixer.saturation) <= 1e-6 &&
          std::abs(mixer.luminance) <= 1e-6) {
        mixer.hue = state.hue_adjustment;
        mixer.saturation = state.saturation_adjustment;
        mixer.luminance = state.luminance_adjustment;
      }
      uniforms.mixer_hue[channel] = static_cast<float>(mixer.hue);
      uniforms.mixer_sat[channel] = static_cast<float>(mixer.saturation);
      uniforms.mixer_lum[channel] = static_cast<float>(mixer.luminance);
    }
    uniforms.use_master_curve = use_master_curve ? 1 : 0;
    uniforms.use_red_curve = use_red_curve ? 1 : 0;
    uniforms.use_green_curve = use_green_curve ? 1 : 0;
    uniforms.use_blue_curve = use_blue_curve ? 1 : 0;
    uniforms.curve_lut_size = kCurveLutSize;
    uniforms.width = width;
    uniforms.height = height;

    id<MTLCommandBuffer> command_buffer = [impl_->command_queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
    [encoder setComputePipelineState:impl_->post_color_pipeline];
    [encoder setBuffer:pixel_buffer offset:0 atIndex:0];
    [encoder setBuffer:master_curve_buffer offset:0 atIndex:1];
    [encoder setBuffer:red_curve_buffer offset:0 atIndex:2];
    [encoder setBuffer:green_curve_buffer offset:0 atIndex:3];
    [encoder setBuffer:blue_curve_buffer offset:0 atIndex:4];
    [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:5];

    NSUInteger thread_group_size = impl_->post_color_pipeline.maxTotalThreadsPerThreadgroup;
    if (thread_group_size > 256) thread_group_size = 256;
    MTLSize grid_size = MTLSizeMake(pixel_count, 1, 1);
    MTLSize group_size = MTLSizeMake(thread_group_size, 1, 1);
    [encoder dispatchThreads:grid_size threadsPerThreadgroup:group_size];
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    copy_pixels_from_float_buffer(pixels, pixel_data);
    return true;
  }
}

bool MetalPipelineBackend::apply_pre_tone_effects(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const std::vector<float>* local_mask_weights,
    double local_mask_exposure,
    double local_mask_temperature) {
  if (!available() || pixels == nullptr || width <= 0 || height <= 0) {
    return false;
  }

  const auto pixel_count = static_cast<std::size_t>(width * height);
  if (pixels->size() != pixel_count) {
    return false;
  }
  if (!state.local_masks.empty() && local_mask_weights == nullptr) {
    return false;
  }
  if (local_mask_weights != nullptr && local_mask_weights->size() != pixel_count) {
    return false;
  }

  constexpr int kCurveLutSize = 256;
  std::vector<float> parametric_curve_lut(static_cast<std::size_t>(kCurveLutSize), 0.0F);
  const auto use_parametric_curve = state.master_curve_points.size() < 2;
  if (use_parametric_curve) {
    for (int index = 0; index < kCurveLutSize; ++index) {
      const auto input = kCurveLutSize > 1
                             ? static_cast<double>(index) / static_cast<double>(kCurveLutSize - 1)
                             : 0.0;
      parametric_curve_lut[static_cast<std::size_t>(index)] =
          static_cast<float>(evaluate_tone_curve(input, state));
    }
  } else {
    for (int index = 0; index < kCurveLutSize; ++index) {
      parametric_curve_lut[static_cast<std::size_t>(index)] =
          kCurveLutSize > 1 ? static_cast<float>(index) / static_cast<float>(kCurveLutSize - 1) : 0.0F;
    }
  }

  @autoreleasepool {
    const auto pixel_buffer_size = pixel_count * sizeof(float) * 4;
    const auto scalar_buffer_size = pixel_count * sizeof(float);
    const auto curve_buffer_size = static_cast<std::size_t>(kCurveLutSize) * sizeof(float);
    id<MTLBuffer> pixel_buffer = [impl_->device newBufferWithLength:pixel_buffer_size
                                                            options:MTLResourceStorageModeShared];
    id<MTLBuffer> target_luminance_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                                       options:MTLResourceStorageModeShared];
    id<MTLBuffer> local_mask_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                                 options:MTLResourceStorageModeShared];
    id<MTLBuffer> curve_buffer = [impl_->device newBufferWithLength:curve_buffer_size
                                                            options:MTLResourceStorageModeShared];
    if (!pixel_buffer || !target_luminance_buffer || !local_mask_buffer || !curve_buffer) {
      return false;
    }

    auto* pixel_data = static_cast<float*>([pixel_buffer contents]);
    copy_pixels_to_float_buffer(pixel_data, *pixels);
    std::copy(parametric_curve_lut.begin(), parametric_curve_lut.end(), static_cast<float*>([curve_buffer contents]));
    auto* local_mask_data = static_cast<float*>([local_mask_buffer contents]);
    if (local_mask_weights != nullptr) {
      std::copy(local_mask_weights->begin(), local_mask_weights->end(), local_mask_data);
    } else {
      std::fill(local_mask_data, local_mask_data + pixel_count, 0.0F);
    }

    const auto global_wb = wb_multipliers(
        effective_input_temperature(state),
        effective_input_tint(state));
    const auto linear_wb = wb_multipliers(state.linear_mask_temperature, 0.0);
    const auto radial_wb = wb_multipliers(state.radial_mask_temperature, 0.0);
    const auto local_wb = wb_multipliers(local_mask_temperature, 0.0);
    const auto use_legacy_masks = state.local_masks.empty();

    PreToneUniforms uniforms{};
    uniforms.exposure_gain = static_cast<float>(std::pow(2.0, effective_input_exposure(state)));
    uniforms.global_wb_red = global_wb[0];
    uniforms.global_wb_green = global_wb[1];
    uniforms.global_wb_blue = global_wb[2];
    uniforms.contrast = static_cast<float>(clamp_signed_percent(state.contrast) / 100.0);
    uniforms.highlights = static_cast<float>(clamp_signed_percent(state.highlights) / 100.0);
    uniforms.shadows = static_cast<float>(clamp_signed_percent(state.shadows) / 100.0);
    uniforms.whites = static_cast<float>(clamp_signed_percent(state.whites) / 100.0);
    uniforms.blacks = static_cast<float>(clamp_signed_percent(state.blacks) / 100.0);
    uniforms.shoulder_threshold = 1.0F;
    uniforms.use_parametric_curve = use_parametric_curve ? 1 : 0;
    uniforms.curve_lut_size = kCurveLutSize;
    uniforms.has_linear_mask = use_legacy_masks && state.linear_mask_enabled ? 1 : 0;
    uniforms.linear_exposure = static_cast<float>(state.linear_mask_exposure);
    uniforms.linear_feather = static_cast<float>(state.linear_mask_feather);
    uniforms.linear_position = static_cast<float>(state.linear_mask_position);
    uniforms.linear_rotation_rad = static_cast<float>(state.linear_mask_rotation * kPi / 180.0);
    uniforms.linear_wb_red = linear_wb[0];
    uniforms.linear_wb_green = linear_wb[1];
    uniforms.linear_wb_blue = linear_wb[2];
    uniforms.has_radial_mask = use_legacy_masks && state.radial_mask_enabled ? 1 : 0;
    uniforms.radial_exposure = static_cast<float>(state.radial_mask_exposure);
    uniforms.radial_feather = static_cast<float>(state.radial_mask_feather);
    uniforms.radial_center_x = static_cast<float>(state.radial_mask_center_x);
    uniforms.radial_center_y = static_cast<float>(state.radial_mask_center_y);
    uniforms.radial_radius_x = static_cast<float>(state.radial_mask_radius_x);
    uniforms.radial_radius_y = static_cast<float>(state.radial_mask_radius_y);
    uniforms.radial_rotation_rad = static_cast<float>(state.radial_mask_rotation * kPi / 180.0);
    uniforms.radial_wb_red = radial_wb[0];
    uniforms.radial_wb_green = radial_wb[1];
    uniforms.radial_wb_blue = radial_wb[2];
    uniforms.has_local_mask = local_mask_weights != nullptr ? 1 : 0;
    uniforms.local_exposure = static_cast<float>(local_mask_exposure);
    uniforms.local_wb_red = local_wb[0];
    uniforms.local_wb_green = local_wb[1];
    uniforms.local_wb_blue = local_wb[2];
    uniforms.width = width;
    uniforms.height = height;

    const auto dispatch_pipeline = [&](id<MTLComputePipelineState> pipeline,
                                       const std::vector<id<MTLBuffer>>& buffers,
                                       int uniforms_index,
                                       const void* raw_uniforms,
                                       size_t uniforms_size) {
      id<MTLCommandBuffer> command_buffer = [impl_->command_queue commandBuffer];
      if (!command_buffer) {
        return;
      }
      id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
      [encoder setComputePipelineState:pipeline];
      for (std::size_t index = 0; index < buffers.size(); ++index) {
        [encoder setBuffer:buffers[index] offset:0 atIndex:index];
      }
      [encoder setBytes:raw_uniforms length:uniforms_size atIndex:static_cast<NSUInteger>(uniforms_index)];
      NSUInteger thread_group_size = pipeline.maxTotalThreadsPerThreadgroup;
      if (thread_group_size > 256) thread_group_size = 256;
      MTLSize grid_size = MTLSizeMake(pixel_count, 1, 1);
      MTLSize group_size = MTLSizeMake(thread_group_size, 1, 1);
      [encoder dispatchThreads:grid_size threadsPerThreadgroup:group_size];
      [encoder endEncoding];
      [command_buffer commit];
      [command_buffer waitUntilCompleted];
    };

    dispatch_pipeline(
        impl_->pre_tone_input_pipeline,
        {pixel_buffer, target_luminance_buffer, local_mask_buffer},
        3,
        &uniforms,
        sizeof(uniforms));

    const auto* target_luminance = static_cast<const float*>([target_luminance_buffer contents]);
    auto max_target_luminance = 0.0;
    for (std::size_t index = 0; index < pixel_count; ++index) {
      max_target_luminance = std::max(max_target_luminance, static_cast<double>(target_luminance[index]));
    }
    uniforms.shoulder_threshold = static_cast<float>(
        max_target_luminance <= 1.0
            ? 1.0
            : std::clamp(0.98 - ((max_target_luminance - 1.0) / (max_target_luminance - 1.0 + 1.0)) * 0.18, 0.82, 0.98));

    dispatch_pipeline(
        impl_->pre_tone_output_pipeline,
        {pixel_buffer, target_luminance_buffer, curve_buffer},
        3,
        &uniforms,
        sizeof(uniforms));

    copy_pixels_from_float_buffer(pixels, pixel_data);
    return true;
  }
}

bool MetalPipelineBackend::apply_detail_effects(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    double sharpen_strength,
    double sharpen_mask_strength,
    double luma_nr_strength,
    double color_nr_strength) {
  if (!available() || pixels == nullptr || width <= 0 || height <= 0) {
    return false;
  }

  const auto pixel_count = static_cast<std::size_t>(width * height);
  if (pixels->size() != pixel_count) {
    return false;
  }

  if (std::abs(sharpen_strength) <= 1e-6 &&
      std::abs(luma_nr_strength) <= 1e-6 &&
      std::abs(color_nr_strength) <= 1e-6) {
    return true;
  }

  @autoreleasepool {
    const auto pixel_buffer_size = pixel_count * sizeof(float) * 4;
    const auto scalar_buffer_size = pixel_count * sizeof(float);
    id<MTLBuffer> pixel_buffer = [impl_->device newBufferWithLength:pixel_buffer_size
                                                            options:MTLResourceStorageModeShared];
    id<MTLBuffer> lightness_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                                options:MTLResourceStorageModeShared];
    id<MTLBuffer> chroma_a_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                               options:MTLResourceStorageModeShared];
    id<MTLBuffer> chroma_b_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                               options:MTLResourceStorageModeShared];
    id<MTLBuffer> scratch_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                              options:MTLResourceStorageModeShared];
    id<MTLBuffer> blur_a_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                             options:MTLResourceStorageModeShared];
    id<MTLBuffer> blur_b_buffer = [impl_->device newBufferWithLength:scalar_buffer_size
                                                             options:MTLResourceStorageModeShared];
    if (!pixel_buffer || !lightness_buffer || !chroma_a_buffer || !chroma_b_buffer ||
        !scratch_buffer || !blur_a_buffer || !blur_b_buffer) {
      return false;
    }

    auto* pixel_data = static_cast<float*>([pixel_buffer contents]);
    copy_pixels_to_float_buffer(pixel_data, *pixels);

    const DetailUniforms uniforms{
        .sharpen_strength = static_cast<float>(sharpen_strength),
        .sharpen_mask_strength = static_cast<float>(sharpen_mask_strength),
        .luma_nr_strength = static_cast<float>(luma_nr_strength),
        .color_nr_strength = static_cast<float>(color_nr_strength),
        .width = width,
        .height = height,
    };

    id<MTLCommandBuffer> command_buffer = [impl_->command_queue commandBuffer];
    if (!command_buffer) {
      return false;
    }

    const auto dispatch_pipeline = [&](id<MTLComputePipelineState> pipeline,
                                       const std::vector<id<MTLBuffer>>& buffers,
                                       int uniforms_index,
                                       const void* raw_uniforms,
                                       size_t uniforms_size) {
      id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
      [encoder setComputePipelineState:pipeline];
      for (std::size_t index = 0; index < buffers.size(); ++index) {
        [encoder setBuffer:buffers[index] offset:0 atIndex:index];
      }
      [encoder setBytes:raw_uniforms length:uniforms_size atIndex:static_cast<NSUInteger>(uniforms_index)];

      NSUInteger thread_group_size = pipeline.maxTotalThreadsPerThreadgroup;
      if (thread_group_size > 256) thread_group_size = 256;
      MTLSize grid_size = MTLSizeMake(pixel_count, 1, 1);
      MTLSize group_size = MTLSizeMake(thread_group_size, 1, 1);
      [encoder dispatchThreads:grid_size threadsPerThreadgroup:group_size];
      [encoder endEncoding];
    };

    const auto copy_buffer = [&](id<MTLBuffer> source, id<MTLBuffer> destination) {
      id<MTLBlitCommandEncoder> encoder = [command_buffer blitCommandEncoder];
      [encoder copyFromBuffer:source sourceOffset:0 toBuffer:destination destinationOffset:0 size:scalar_buffer_size];
      [encoder endEncoding];
    };

    const auto run_gaussian_blur = [&](id<MTLBuffer> source, id<MTLBuffer> output, int radius) {
      copy_buffer(source, output);
      if (radius <= 0) {
        return;
      }

      for (const auto pass_radius : gaussian_approximation_radii(static_cast<double>(radius))) {
        if (pass_radius <= 0) {
          continue;
        }

        const BlurUniforms blur_uniforms{
            .width = width,
            .height = height,
            .radius = pass_radius,
        };
        dispatch_pipeline(
            impl_->blur_horizontal_pipeline,
            {output, scratch_buffer},
            2,
            &blur_uniforms,
            sizeof(blur_uniforms));
        dispatch_pipeline(
            impl_->blur_vertical_pipeline,
            {scratch_buffer, output},
            2,
            &blur_uniforms,
            sizeof(blur_uniforms));
      }
    };

    dispatch_pipeline(
        impl_->extract_oklab_pipeline,
        {pixel_buffer, lightness_buffer, chroma_a_buffer, chroma_b_buffer},
        4,
        &uniforms,
        sizeof(uniforms));

    if (std::abs(sharpen_strength) > 1e-6) {
      run_gaussian_blur(lightness_buffer, blur_a_buffer, 1);
      run_gaussian_blur(lightness_buffer, blur_b_buffer, 3);
      dispatch_pipeline(
          impl_->sharpen_pipeline,
          {lightness_buffer, blur_a_buffer, blur_b_buffer},
          3,
          &uniforms,
          sizeof(uniforms));
    }

    if (std::abs(luma_nr_strength) > 1e-6) {
      run_gaussian_blur(lightness_buffer, blur_a_buffer, 2);
      run_gaussian_blur(lightness_buffer, blur_b_buffer, 5);
      dispatch_pipeline(
          impl_->luma_nr_pipeline,
          {lightness_buffer, blur_a_buffer, blur_b_buffer},
          3,
          &uniforms,
          sizeof(uniforms));
    }

    if (std::abs(color_nr_strength) > 1e-6) {
      run_gaussian_blur(chroma_a_buffer, blur_a_buffer, 3);
      run_gaussian_blur(chroma_b_buffer, blur_b_buffer, 3);
      dispatch_pipeline(
          impl_->color_nr_pipeline,
          {chroma_a_buffer, chroma_b_buffer, blur_a_buffer, blur_b_buffer},
          4,
          &uniforms,
          sizeof(uniforms));
    }

    dispatch_pipeline(
        impl_->compose_detail_pipeline,
        {pixel_buffer, lightness_buffer, chroma_a_buffer, chroma_b_buffer},
        4,
        &uniforms,
        sizeof(uniforms));

    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    copy_pixels_from_float_buffer(pixels, pixel_data);
    return true;
  }
}

}  // namespace photo_editor::pipeline::metal
