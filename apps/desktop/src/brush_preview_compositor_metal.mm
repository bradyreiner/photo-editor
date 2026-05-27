#include "brush_preview_compositor.h"

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <memory>
#include <simd/simd.h>

namespace photo_editor::desktop {

namespace {

struct BrushUniforms {
  simd_float2 center;
  float size;
  float flow;
  float feather;
  float hardness;
  float opacity;
  simd_float4 color;
  simd_uint2 texture_size;
  simd_uint2 origin;
};

constexpr auto kBrushPreviewShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct BrushUniforms {
  float2 center;
  float size;
  float flow;
  float feather;
  float hardness;
  float opacity;
  float4 color;
  uint2 texture_size;
  uint2 origin;
};

float smoothstep_safe(float edge0, float edge1, float x) {
  if (edge0 >= edge1) {
    return x < edge0 ? 0.0 : 1.0;
  }
  float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

kernel void clear_brush_preview(
    texture2d<half, access::write> target [[texture(0)]],
    uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= target.get_width() || gid.y >= target.get_height()) {
    return;
  }
  target.write(half4(0.0), gid);
}

kernel void composite_brush_dab(
    texture2d<half, access::read_write> target [[texture(0)]],
    constant BrushUniforms& uniforms [[buffer(0)]],
    uint2 local_gid [[thread_position_in_grid]]) {
  uint2 gid = uniforms.origin + local_gid;
  if (gid.x >= uniforms.texture_size.x || gid.y >= uniforms.texture_size.y) {
    return;
  }

  float denom_x = max(float(max(int(uniforms.texture_size.x) - 1, 1)), 1.0);
  float denom_y = max(float(max(int(uniforms.texture_size.y) - 1, 1)), 1.0);
  float2 pixel = float2(float(gid.x) / denom_x, float(gid.y) / denom_y);
  float distance_norm = distance(pixel, uniforms.center) / max(uniforms.size, 0.01);
  float inner_scale = clamp(uniforms.hardness, 0.0, 0.98);
  float weight = uniforms.flow * (1.0 - smoothstep_safe(inner_scale, 1.0, distance_norm));
  float src_alpha = clamp(weight * uniforms.opacity, 0.0, 1.0);
  if (src_alpha <= 1e-6) {
    return;
  }

  half4 dst_half = target.read(gid);
  float4 dst = float4(dst_half);
  float out_alpha = src_alpha + dst.a * (1.0 - src_alpha);
  if (out_alpha <= 1e-6) {
    return;
  }

  float3 out_rgb =
      (uniforms.color.rgb * src_alpha + dst.rgb * dst.a * (1.0 - src_alpha)) / out_alpha;
  target.write(half4(half3(out_rgb), half(out_alpha)), gid);
}
)";

class MetalBrushPreviewCompositor final : public BrushPreviewCompositor {
 public:
  MetalBrushPreviewCompositor() {
    @autoreleasepool {
      device_ = MTLCreateSystemDefaultDevice();
      if (device_ == nil) {
        return;
      }

      command_queue_ = [device_ newCommandQueue];
      if (command_queue_ == nil) {
        return;
      }

      NSError* error = nil;
      auto* source = [NSString stringWithUTF8String:kBrushPreviewShaderSource];
      auto* library = [device_ newLibraryWithSource:source options:nil error:&error];
      if (library == nil || error != nil) {
        return;
      }

      auto* clear_function = [library newFunctionWithName:@"clear_brush_preview"];
      auto* composite_function = [library newFunctionWithName:@"composite_brush_dab"];
      if (clear_function == nil || composite_function == nil) {
        return;
      }

      clear_pipeline_ = [device_ newComputePipelineStateWithFunction:clear_function error:&error];
      if (clear_pipeline_ == nil || error != nil) {
        return;
      }

      error = nil;
      composite_pipeline_ = [device_ newComputePipelineStateWithFunction:composite_function error:&error];
      if (composite_pipeline_ == nil || error != nil) {
        return;
      }

      available_ = true;
    }
  }

  [[nodiscard]] bool available() const override { return available_; }
  [[nodiscard]] const char* backendName() const override { return "metal"; }
  [[nodiscard]] bool hasNativeTexture() const override { return available_ && texture_ != nil; }
  [[nodiscard]] quint64 nativeTextureHandle() const override {
    return hasNativeTexture() ? static_cast<quint64>(reinterpret_cast<quintptr>(texture_)) : 0;
  }
  [[nodiscard]] QSize nativeTextureSize() const override {
    return hasNativeTexture()
               ? QSize(static_cast<int>(texture_.width), static_cast<int>(texture_.height))
               : QSize{};
  }

  bool ensureTarget(QImage* image, const QSize& size) const override {
    if (!available_ || image == nullptr || !size.isValid() || size.width() <= 1 || size.height() <= 1) {
      return false;
    }
    *image = QImage{};

    if (texture_ != nil &&
        static_cast<int>(texture_.width) == size.width() &&
        static_cast<int>(texture_.height) == size.height()) {
      return true;
    }

    auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                           width:static_cast<NSUInteger>(size.width())
                                                                          height:static_cast<NSUInteger>(size.height())
                                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModeShared;
    texture_ = [device_ newTextureWithDescriptor:descriptor];
    if (texture_ == nil) {
      return false;
    }
    clear(image);
    return true;
  }

  void clear(QImage* image) const override {
    if (!available_ || image == nullptr || image->isNull() || texture_ == nil) {
      if (image != nullptr) {
        *image = QImage{};
      }
      if (!available_ || texture_ == nil) {
        return;
      }
    }
    if (image != nullptr) {
      *image = QImage{};
    }
    auto* command_buffer = [command_queue_ commandBuffer];
    auto* encoder = [command_buffer computeCommandEncoder];
    [encoder setComputePipelineState:clear_pipeline_];
    [encoder setTexture:texture_ atIndex:0];

    const auto width = texture_.width;
    const auto height = texture_.height;
    const auto thread_width = std::min<NSUInteger>(clear_pipeline_.threadExecutionWidth, width);
    const auto max_threads =
        std::max<NSUInteger>(1, clear_pipeline_.maxTotalThreadsPerThreadgroup / std::max<NSUInteger>(thread_width, 1));
    const auto thread_height = std::min<NSUInteger>(max_threads, height);
    const auto threads = MTLSizeMake(width, height, 1);
    const auto group = MTLSizeMake(std::max<NSUInteger>(1, thread_width), std::max<NSUInteger>(1, thread_height), 1);
    [encoder dispatchThreads:threads threadsPerThreadgroup:group];
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
  }

  void compositePoints(
      QImage* image,
      const QVector<BrushPreviewPoint>& points,
      const BrushPreviewStyle& style) const override {
    if (!available_ || image == nullptr || image->isNull() || texture_ == nil || points.isEmpty()) {
      return;
    }

    auto* command_buffer = [command_queue_ commandBuffer];
    auto* encoder = [command_buffer computeCommandEncoder];
    [encoder setComputePipelineState:composite_pipeline_];
    [encoder setTexture:texture_ atIndex:0];

    for (const auto& point : points) {
      const auto clamped_size = std::max(0.01, point.size);
      const auto radius_x = std::max(
          2,
          static_cast<int>(std::ceil(clamped_size * static_cast<double>(std::max(image->width() - 1, 1)))));
      const auto radius_y = std::max(
          2,
          static_cast<int>(std::ceil(clamped_size * static_cast<double>(std::max(image->height() - 1, 1)))));
      const auto anchor_x = std::clamp(point.x, 0.0, 1.0) * static_cast<double>(std::max(image->width() - 1, 1));
      const auto anchor_y = std::clamp(point.y, 0.0, 1.0) * static_cast<double>(std::max(image->height() - 1, 1));
      const auto min_x = std::max(0, static_cast<int>(std::floor(anchor_x)) - radius_x);
      const auto max_x = std::min(image->width() - 1, static_cast<int>(std::ceil(anchor_x)) + radius_x);
      const auto min_y = std::max(0, static_cast<int>(std::floor(anchor_y)) - radius_y);
      const auto max_y = std::min(image->height() - 1, static_cast<int>(std::ceil(anchor_y)) + radius_y);
      const auto dispatch_width = std::max(0, max_x - min_x + 1);
      const auto dispatch_height = std::max(0, max_y - min_y + 1);
      if (dispatch_width <= 0 || dispatch_height <= 0) {
        continue;
      }

      const auto thread_width =
          std::min<NSUInteger>(composite_pipeline_.threadExecutionWidth, static_cast<NSUInteger>(dispatch_width));
      const auto max_threads = std::max<NSUInteger>(
          1,
          composite_pipeline_.maxTotalThreadsPerThreadgroup / std::max<NSUInteger>(thread_width, 1));
      const auto thread_height =
          std::min<NSUInteger>(max_threads, static_cast<NSUInteger>(dispatch_height));

      const auto uniforms = BrushUniforms{
          .center = simd_make_float2(static_cast<float>(point.x), static_cast<float>(point.y)),
          .size = static_cast<float>(clamped_size),
          .flow = static_cast<float>(std::clamp(point.flow, 0.0, 1.0)),
          .feather = static_cast<float>(std::clamp(style.feather, 0.05, 1.0)),
          .hardness = static_cast<float>(std::clamp(style.hardness, 0.0, 0.98)),
          .opacity = static_cast<float>(std::clamp(style.opacity, 0.0, 1.0)),
          .color = simd_make_float4(
              static_cast<float>(style.color.redF()),
              static_cast<float>(style.color.greenF()),
              static_cast<float>(style.color.blueF()),
              1.0f),
          .texture_size = simd_make_uint2(
              static_cast<unsigned int>(image->width()),
              static_cast<unsigned int>(image->height())),
          .origin = simd_make_uint2(
              static_cast<unsigned int>(min_x),
              static_cast<unsigned int>(min_y)),
      };

      [encoder setBytes:&uniforms length:sizeof(BrushUniforms) atIndex:0];
      [encoder dispatchThreads:MTLSizeMake(
                                   static_cast<NSUInteger>(dispatch_width),
                                   static_cast<NSUInteger>(dispatch_height),
                                   1)
          threadsPerThreadgroup:MTLSizeMake(
                                     std::max<NSUInteger>(1, thread_width),
                                     std::max<NSUInteger>(1, thread_height),
                                     1)];
    }

    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
  }

  mutable id<MTLDevice> device_ = nil;
  mutable id<MTLCommandQueue> command_queue_ = nil;
  mutable id<MTLComputePipelineState> clear_pipeline_ = nil;
  mutable id<MTLComputePipelineState> composite_pipeline_ = nil;
  mutable id<MTLTexture> texture_ = nil;
  bool available_ = false;
};

}  // namespace

std::unique_ptr<BrushPreviewCompositor> create_metal_brush_preview_compositor() {
  auto compositor = std::make_unique<MetalBrushPreviewCompositor>();
  return compositor->available() ? std::unique_ptr<BrushPreviewCompositor>(std::move(compositor)) : nullptr;
}

}  // namespace photo_editor::desktop

#endif
