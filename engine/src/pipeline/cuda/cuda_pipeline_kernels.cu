#include "cuda_pipeline_kernels.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace {

void write_error(char* output, size_t capacity, const char* message) {
  if (output == NULL || capacity == 0U) {
    return;
  }
  std::snprintf(output, capacity, "%s", message != NULL ? message : "");
}

void write_cuda_error(
    char* output,
    size_t capacity,
    const char* prefix,
    cudaError_t status) {
  if (output == NULL || capacity == 0U) {
    return;
  }
  std::snprintf(
      output,
      capacity,
      "%s: %s",
      prefix,
      cudaGetErrorString(status));
}

__device__ float clamp_channel(float value, float minimum, float maximum) {
  return fminf(fmaxf(value, minimum), maximum);
}

__device__ float3 add_scaled(
    float3 result,
    const float4& value,
    float weight) {
  result.x += value.x * weight;
  result.y += value.y * weight;
  result.z += value.z * weight;
  return result;
}

__device__ int lut_index(int size, int red, int green, int blue) {
  return (red * size + green) * size + blue;
}

__global__ void apply_scene_exposure_kernel(
    float4* pixels,
    size_t pixel_count,
    float exposure_gain) {
  const size_t index = static_cast<size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
  if (index >= pixel_count) {
    return;
  }
  float4 pixel = pixels[index];
  pixel.x *= exposure_gain;
  pixel.y *= exposure_gain;
  pixel.z *= exposure_gain;
  pixels[index] = pixel;
}

__device__ float3 tetrahedral_sample(
    const float4* lut,
    int size,
    float3 coordinate) {
  const float red_position =
      clamp_channel(coordinate.x, 0.0F, 1.0F) * static_cast<float>(size - 1);
  const float green_position =
      clamp_channel(coordinate.y, 0.0F, 1.0F) * static_cast<float>(size - 1);
  const float blue_position =
      clamp_channel(coordinate.z, 0.0F, 1.0F) * static_cast<float>(size - 1);
  const int red = min(static_cast<int>(red_position), size - 2);
  const int green = min(static_cast<int>(green_position), size - 2);
  const int blue = min(static_cast<int>(blue_position), size - 2);
  const float red_fraction = red_position - static_cast<float>(red);
  const float green_fraction = green_position - static_cast<float>(green);
  const float blue_fraction = blue_position - static_cast<float>(blue);
  const float4 c000 = lut[lut_index(size, red, green, blue)];
  const float4 c111 = lut[lut_index(size, red + 1, green + 1, blue + 1)];
  float4 v1 = make_float4(0.0F, 0.0F, 0.0F, 0.0F);
  float4 v2 = make_float4(0.0F, 0.0F, 0.0F, 0.0F);
  float weights[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  if (red_fraction >= green_fraction) {
    if (green_fraction >= blue_fraction) {
      v1 = lut[lut_index(size, red + 1, green, blue)];
      v2 = lut[lut_index(size, red + 1, green + 1, blue)];
      weights[0] = 1.0F - red_fraction;
      weights[1] = red_fraction - green_fraction;
      weights[2] = green_fraction - blue_fraction;
      weights[3] = blue_fraction;
    } else if (red_fraction >= blue_fraction) {
      v1 = lut[lut_index(size, red + 1, green, blue)];
      v2 = lut[lut_index(size, red + 1, green, blue + 1)];
      weights[0] = 1.0F - red_fraction;
      weights[1] = red_fraction - blue_fraction;
      weights[2] = blue_fraction - green_fraction;
      weights[3] = green_fraction;
    } else {
      v1 = lut[lut_index(size, red, green, blue + 1)];
      v2 = lut[lut_index(size, red + 1, green, blue + 1)];
      weights[0] = 1.0F - blue_fraction;
      weights[1] = blue_fraction - red_fraction;
      weights[2] = red_fraction - green_fraction;
      weights[3] = green_fraction;
    }
  } else if (red_fraction >= blue_fraction) {
    v1 = lut[lut_index(size, red, green + 1, blue)];
    v2 = lut[lut_index(size, red + 1, green + 1, blue)];
    weights[0] = 1.0F - green_fraction;
    weights[1] = green_fraction - red_fraction;
    weights[2] = red_fraction - blue_fraction;
    weights[3] = blue_fraction;
  } else if (green_fraction >= blue_fraction) {
    v1 = lut[lut_index(size, red, green + 1, blue)];
    v2 = lut[lut_index(size, red, green + 1, blue + 1)];
    weights[0] = 1.0F - green_fraction;
    weights[1] = green_fraction - blue_fraction;
    weights[2] = blue_fraction - red_fraction;
    weights[3] = red_fraction;
  } else {
    v1 = lut[lut_index(size, red, green, blue + 1)];
    v2 = lut[lut_index(size, red, green + 1, blue + 1)];
    weights[0] = 1.0F - blue_fraction;
    weights[1] = blue_fraction - green_fraction;
    weights[2] = green_fraction - red_fraction;
    weights[3] = red_fraction;
  }
  float3 result = make_float3(0.0F, 0.0F, 0.0F);
  result = add_scaled(result, c000, weights[0]);
  result = add_scaled(result, v1, weights[1]);
  result = add_scaled(result, v2, weights[2]);
  return add_scaled(result, c111, weights[3]);
}

__device__ float apply_black_point_compensation(
    float value,
    const PhotoEditorCudaAces2Uniforms& uniforms) {
  if (uniforms.black_point_compensation_enabled == 0) {
    return value;
  }
  if (value <= uniforms.source_black_point) {
    return uniforms.output_black_point;
  }
  const float source_span = fmaxf(
      1.0e-6F,
      uniforms.black_point_headroom - uniforms.source_black_point);
  const float output_span = fmaxf(
      0.0F,
      uniforms.black_point_headroom - uniforms.output_black_point);
  const float normalized =
      (value - uniforms.source_black_point) / source_span;
  return clamp_channel(
      uniforms.output_black_point + normalized * output_span,
      0.0F,
      uniforms.black_point_headroom);
}

__global__ void apply_aces2_output_lut_kernel(
    float4* pixels,
    size_t pixel_count,
    const float4* lut,
    PhotoEditorCudaAces2Uniforms uniforms) {
  const size_t index = static_cast<size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
  if (index >= pixel_count) {
    return;
  }
  const float4 source = pixels[index];
  float3 acescg = make_float3(
      uniforms.working_to_acescg[0] * source.x +
          uniforms.working_to_acescg[1] * source.y +
          uniforms.working_to_acescg[2] * source.z,
      uniforms.working_to_acescg[3] * source.x +
          uniforms.working_to_acescg[4] * source.y +
          uniforms.working_to_acescg[5] * source.z,
      uniforms.working_to_acescg[6] * source.x +
          uniforms.working_to_acescg[7] * source.y +
          uniforms.working_to_acescg[8] * source.z);
  acescg.x = clamp_channel(acescg.x, 0.0F, uniforms.shaper_maximum);
  acescg.y = clamp_channel(acescg.y, 0.0F, uniforms.shaper_maximum);
  acescg.z = clamp_channel(acescg.z, 0.0F, uniforms.shaper_maximum);
  const float denominator =
      log1pf(uniforms.shaper_maximum / uniforms.shaper_offset);
  const float3 coordinate = make_float3(
      log1pf(acescg.x / uniforms.shaper_offset) / denominator,
      log1pf(acescg.y / uniforms.shaper_offset) / denominator,
      log1pf(acescg.z / uniforms.shaper_offset) / denominator);
  float3 target = tetrahedral_sample(lut, uniforms.lut_size, coordinate);
  target.x = clamp_channel(
                 target.x, 0.0F, uniforms.maximum_physical_rgb) *
             uniforms.physical_to_reference_white;
  target.y = clamp_channel(
                 target.y, 0.0F, uniforms.maximum_physical_rgb) *
             uniforms.physical_to_reference_white;
  target.z = clamp_channel(
                 target.z, 0.0F, uniforms.maximum_physical_rgb) *
             uniforms.physical_to_reference_white;
  target.x = apply_black_point_compensation(target.x, uniforms);
  target.y = apply_black_point_compensation(target.y, uniforms);
  target.z = apply_black_point_compensation(target.z, uniforms);
  pixels[index] = make_float4(
      clamp_channel(target.x, 0.0F, uniforms.display_headroom),
      clamp_channel(target.y, 0.0F, uniforms.display_headroom),
      clamp_channel(target.z, 0.0F, uniforms.display_headroom),
      clamp_channel(source.w, 0.0F, 1.0F));
}

struct CudaContext {
  std::mutex mutex;
  int device_index;
  float4* cached_lut;
  size_t cached_lut_bytes;
  uint64_t cached_lut_identity;

  CudaContext()
      : device_index(0),
        cached_lut(NULL),
        cached_lut_bytes(0U),
        cached_lut_identity(0U) {}
};

}  // namespace

extern "C" void* photo_editor_cuda_create(
    char* error,
    size_t error_capacity,
    char* device_name,
    size_t device_name_capacity) {
  write_error(error, error_capacity, "");
  write_error(device_name, device_name_capacity, "");
  int count = 0;
  cudaError_t status = cudaGetDeviceCount(&count);
  if (status != cudaSuccess) {
    write_cuda_error(
        error, error_capacity, "cuda_device_enumeration_failed", status);
    (void)cudaGetLastError();
    return NULL;
  }
  if (count <= 0) {
    write_error(error, error_capacity, "cuda_device_not_found");
    return NULL;
  }
  CudaContext* context = new CudaContext();
  cudaDeviceProp properties;
  std::memset(&properties, 0, sizeof(properties));
  status = cudaGetDeviceProperties(&properties, context->device_index);
  // The build contains sm_75 and sm_86 images (including PTX for forward JIT),
  // so lower compute capabilities cannot execute these kernels.
  const bool supports_compiled_architectures =
      properties.major > 7 ||
      (properties.major == 7 && properties.minor >= 5);
  if (status != cudaSuccess || !supports_compiled_architectures) {
    write_error(
        error,
        error_capacity,
        status == cudaSuccess
            ? "cuda_device_compute_capability_below_7_5"
            : "cuda_device_properties_failed");
    delete context;
    return NULL;
  }
  status = cudaSetDevice(context->device_index);
  if (status == cudaSuccess) {
    status = cudaFree(NULL);
  }
  if (status != cudaSuccess) {
    write_cuda_error(
        error, error_capacity, "cuda_context_initialization_failed", status);
    delete context;
    return NULL;
  }
  write_error(device_name, device_name_capacity, properties.name);
  return context;
}

extern "C" void photo_editor_cuda_destroy(void* opaque_context) {
  CudaContext* context = static_cast<CudaContext*>(opaque_context);
  if (context == NULL) {
    return;
  }
  if (context->cached_lut != NULL) {
    (void)cudaFree(context->cached_lut);
  }
  delete context;
}

extern "C" int photo_editor_cuda_apply_scene_exposure(
    void* opaque_context,
    float* host_pixels_rgba,
    size_t pixel_count,
    float exposure_gain,
    char* error,
    size_t error_capacity) {
  CudaContext* context = static_cast<CudaContext*>(opaque_context);
  write_error(error, error_capacity, "");
  if (context == NULL || host_pixels_rgba == NULL || pixel_count == 0U ||
      !std::isfinite(exposure_gain) || exposure_gain <= 0.0F) {
    write_error(error, error_capacity, "invalid_cuda_scene_exposure_request");
    return 0;
  }
  std::lock_guard<std::mutex> lock(context->mutex);
  cudaError_t status = cudaSetDevice(context->device_index);
  const size_t pixel_bytes = pixel_count * sizeof(float4);
  float4* device_pixels = NULL;
  if (status == cudaSuccess) {
    status = cudaMalloc(
        reinterpret_cast<void**>(&device_pixels), pixel_bytes);
  }
  if (status == cudaSuccess) {
    status = cudaMemcpy(
        device_pixels,
        host_pixels_rgba,
        pixel_bytes,
        cudaMemcpyHostToDevice);
  }
  if (status != cudaSuccess) {
    if (device_pixels != NULL) {
      (void)cudaFree(device_pixels);
    }
    write_cuda_error(
        error, error_capacity, "cuda_scene_exposure_upload_failed", status);
    return 0;
  }
  const unsigned int threads_per_block = 256U;
  const unsigned int block_count = static_cast<unsigned int>(
      (pixel_count + threads_per_block - 1U) / threads_per_block);
  apply_scene_exposure_kernel<<<block_count, threads_per_block>>>(
      device_pixels, pixel_count, exposure_gain);
  status = cudaGetLastError();
  if (status == cudaSuccess) {
    status = cudaDeviceSynchronize();
  }
  if (status == cudaSuccess) {
    status = cudaMemcpy(
        host_pixels_rgba,
        device_pixels,
        pixel_bytes,
        cudaMemcpyDeviceToHost);
  }
  (void)cudaFree(device_pixels);
  if (status != cudaSuccess) {
    write_cuda_error(
        error, error_capacity, "cuda_scene_exposure_kernel_failed", status);
    return 0;
  }
  return 1;
}

extern "C" int photo_editor_cuda_apply_aces2_output_lut(
    void* opaque_context,
    float* host_pixels_rgba,
    size_t pixel_count,
    const float* host_lut_rgba,
    size_t lut_entry_count,
    uint64_t lut_identity,
    const PhotoEditorCudaAces2Uniforms* uniforms,
    size_t* uploaded_lut_bytes,
    char* error,
    size_t error_capacity) {
  CudaContext* context = static_cast<CudaContext*>(opaque_context);
  write_error(error, error_capacity, "");
  if (uploaded_lut_bytes != NULL) {
    *uploaded_lut_bytes = 0U;
  }
  if (context == NULL || host_pixels_rgba == NULL || pixel_count == 0U ||
      host_lut_rgba == NULL || lut_entry_count == 0U || uniforms == NULL ||
      uniforms->lut_size < 2) {
    write_error(error, error_capacity, "invalid_cuda_aces2_kernel_request");
    return 0;
  }
  std::lock_guard<std::mutex> lock(context->mutex);
  cudaError_t status = cudaSetDevice(context->device_index);
  if (status != cudaSuccess) {
    write_cuda_error(error, error_capacity, "cuda_set_device_failed", status);
    return 0;
  }
  const size_t lut_bytes = lut_entry_count * sizeof(float4);
  if (context->cached_lut == NULL ||
      context->cached_lut_identity != lut_identity ||
      context->cached_lut_bytes != lut_bytes) {
    if (context->cached_lut != NULL) {
      (void)cudaFree(context->cached_lut);
      context->cached_lut = NULL;
    }
    status = cudaMalloc(
        reinterpret_cast<void**>(&context->cached_lut), lut_bytes);
    if (status != cudaSuccess) {
      write_cuda_error(
          error, error_capacity, "cuda_aces2_lut_allocation_failed", status);
      return 0;
    }
    status = cudaMemcpy(
        context->cached_lut,
        host_lut_rgba,
        lut_bytes,
        cudaMemcpyHostToDevice);
    if (status != cudaSuccess) {
      (void)cudaFree(context->cached_lut);
      context->cached_lut = NULL;
      write_cuda_error(
          error, error_capacity, "cuda_aces2_lut_upload_failed", status);
      return 0;
    }
    context->cached_lut_identity = lut_identity;
    context->cached_lut_bytes = lut_bytes;
    if (uploaded_lut_bytes != NULL) {
      *uploaded_lut_bytes = lut_bytes;
    }
  }
  const size_t pixel_bytes = pixel_count * sizeof(float4);
  float4* device_pixels = NULL;
  status = cudaMalloc(reinterpret_cast<void**>(&device_pixels), pixel_bytes);
  if (status == cudaSuccess) {
    status = cudaMemcpy(
        device_pixels,
        host_pixels_rgba,
        pixel_bytes,
        cudaMemcpyHostToDevice);
  }
  if (status != cudaSuccess) {
    if (device_pixels != NULL) {
      (void)cudaFree(device_pixels);
    }
    write_cuda_error(
        error, error_capacity, "cuda_aces2_pixel_upload_failed", status);
    return 0;
  }
  const unsigned int threads_per_block = 256U;
  const unsigned int block_count = static_cast<unsigned int>(
      (pixel_count + threads_per_block - 1U) / threads_per_block);
  apply_aces2_output_lut_kernel<<<block_count, threads_per_block>>>(
      device_pixels,
      pixel_count,
      context->cached_lut,
      *uniforms);
  status = cudaGetLastError();
  if (status == cudaSuccess) {
    status = cudaDeviceSynchronize();
  }
  if (status == cudaSuccess) {
    status = cudaMemcpy(
        host_pixels_rgba,
        device_pixels,
        pixel_bytes,
        cudaMemcpyDeviceToHost);
  }
  (void)cudaFree(device_pixels);
  if (status != cudaSuccess) {
    write_cuda_error(error, error_capacity, "cuda_aces2_kernel_failed", status);
    return 0;
  }
  return 1;
}
