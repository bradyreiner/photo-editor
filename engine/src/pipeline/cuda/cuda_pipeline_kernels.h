#pragma once

#include <stddef.h>
#include <stdint.h>

struct PhotoEditorCudaAces2Uniforms {
  float working_to_acescg[9];
  float shaper_maximum;
  float shaper_offset;
  float maximum_physical_rgb;
  float physical_to_reference_white;
  float source_black_point;
  float output_black_point;
  float black_point_headroom;
  float display_headroom;
  int black_point_compensation_enabled;
  int lut_size;
};

extern "C" {

void* photo_editor_cuda_create(
    char* error,
    size_t error_capacity,
    char* device_name,
    size_t device_name_capacity);

void photo_editor_cuda_destroy(void* context);

int photo_editor_cuda_apply_scene_exposure(
    void* context,
    float* host_pixels_rgba,
    size_t pixel_count,
    float exposure_gain,
    char* error,
    size_t error_capacity);

int photo_editor_cuda_apply_aces2_output_lut(
    void* context,
    float* host_pixels_rgba,
    size_t pixel_count,
    const float* host_lut_rgba,
    size_t lut_entry_count,
    uint64_t lut_identity,
    const PhotoEditorCudaAces2Uniforms* uniforms,
    size_t* uploaded_lut_bytes,
    char* error,
    size_t error_capacity);

}  // extern "C"
