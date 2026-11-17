#include "timeline_desktop_light_field_metal.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <dispatch/dispatch.h>

#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QVector>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace photo_editor::desktop::timeline_motion {
namespace {

constexpr const char* kLightFieldMetalSource = R"metal(
#include <metal_stdlib>
using namespace metal;

struct LightParameters {
  uint width;
  uint height;
  uint rowBytes;
  uint noiseWidth;
  uint noiseHeight;
  uint grainWidth;
  uint grainHeight;
  int noiseMinX;
  int noiseMinY;
  float timeSeconds;
  float gather;
  float opacity;
};

float lookupNoiseHash(int2 cell, constant LightParameters& parameters,
                      device const float* hashTable) {
  int2 tableCell = clamp(cell - int2(parameters.noiseMinX, parameters.noiseMinY),
                         int2(0),
                         int2(parameters.noiseWidth - 1, parameters.noiseHeight - 1));
  return hashTable[uint(tableCell.y) * parameters.noiseWidth + uint(tableCell.x)];
}

float fieldNoise(float2 p, constant LightParameters& parameters,
                 device const float* hashTable) {
  float2 cell = floor(p);
  float2 fraction = fract(p);
  fraction = fraction * fraction * (3.0 - 2.0 * fraction);
  int2 integerCell = int2(cell);
  float lower = mix(lookupNoiseHash(integerCell, parameters, hashTable),
                    lookupNoiseHash(integerCell + int2(1, 0), parameters, hashTable),
                    fraction.x);
  float upper = mix(lookupNoiseHash(integerCell + int2(0, 1), parameters, hashTable),
                    lookupNoiseHash(integerCell + int2(1, 1), parameters, hashTable),
                    fraction.x);
  return mix(lower, upper, fraction.y);
}

float fieldFbm(float2 p, constant LightParameters& parameters,
               device const float* hashTable) {
  return 0.57 * fieldNoise(p, parameters, hashTable) +
         0.28 * fieldNoise(p * 2.03, parameters, hashTable) +
         0.15 * fieldNoise(p * 4.07, parameters, hashTable);
}

float lightChannel(float2 p, float channel, float aspectRatio,
                   float timeSeconds, float gather,
                   constant LightParameters& parameters,
                   device const float* hashTable) {
  float phase = timeSeconds * 0.57 + channel * 0.56;
  float noiseValue = fieldFbm(p * 4.0 + float2(timeSeconds * 0.12,
                                               -timeSeconds * 0.08),
                              parameters, hashTable);
  float centre = 0.51 + 0.19 * sin(p.x * 4.3 - phase) +
                 0.055 * cos(p.x * 9.1 + phase);
  centre += (channel - 1.0) * 0.075;
  float band = p.y - centre + (noiseValue - 0.5) * 0.07;
  float thickness = 0.085 + 0.055 * (0.5 + 0.5 * sin(p.x * 4.8 + phase));
  float sheet = exp(-pow(band / thickness, 4.0));
  float core = exp(-pow((band + 0.013 * sin(p.x * 15.0 + phase)) /
                        (thickness * 0.2), 2.0));
  float2 ringVector = (p - float2(0.5, 0.52) +
                       float2((channel - 1.0) * 0.04, 0.0)) *
                      float2(1.0, aspectRatio);
  float rings = length(ringVector);
  float disk = exp(-pow((rings - 0.29) / 0.075, 2.0));
  float caustic = pow(0.5 + 0.5 * sin(noiseValue * 15.0 + band * 25.0 -
                                      timeSeconds * 0.2), 3.0);
  float dispersed = sheet * 0.53 + core * 0.17 + caustic * sheet * 0.45;
  float collected = disk * (0.4 + 0.6 * caustic);
  return mix(dispersed, collected, clamp(gather, 0.0, 1.0));
}

uchar quantizeUnorm8(float value) {
  return uchar(clamp(floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5), 0.0, 255.0));
}

kernel void renderDesktopLightField(
    constant LightParameters& parameters [[buffer(0)]],
    device const float* noiseHashTable [[buffer(1)]],
    device const float* grainHashTable [[buffer(2)]],
    device uchar* output [[buffer(3)]],
    uint2 pixel [[thread_position_in_grid]]) {
  if (pixel.x >= parameters.width || pixel.y >= parameters.height) {
    return;
  }

  float width = float(parameters.width);
  float height = float(parameters.height);
  float x = (float(pixel.x) + 0.5) / width;
  float y = (float(pixel.y) + 0.5) / height;
  float glY = height - float(pixel.y) - 0.5;
  float2 point = float2(x, y);
  uint grainX = uint(floor((float(pixel.x) + 0.5) * 0.7));
  uint grainY = uint(floor(glY * 0.7));
  float grain = grainHashTable[grainY * parameters.grainWidth + grainX];
  float grainGain = 0.67 + 0.48 * grain;
  float aspectRatio = height / width;
  float3 field = float3(
      0.043 + pow(lightChannel(point, 0.0, aspectRatio,
                               parameters.timeSeconds, parameters.gather,
                               parameters, noiseHashTable) * grainGain, 0.8) * 0.94,
      0.051 + pow(lightChannel(point, 1.0, aspectRatio,
                               parameters.timeSeconds, parameters.gather,
                               parameters, noiseHashTable) * grainGain, 0.8) * 0.94,
      0.071 + pow(lightChannel(point, 2.0, aspectRatio,
                               parameters.timeSeconds, parameters.gather,
                               parameters, noiseHashTable) * grainGain, 0.8) * 0.94);
  uint offset = pixel.y * parameters.rowBytes + pixel.x * 4;
  output[offset] = quantizeUnorm8(field.r);
  output[offset + 1] = quantizeUnorm8(field.g);
  output[offset + 2] = quantizeUnorm8(field.b);
  output[offset + 3] = quantizeUnorm8(parameters.opacity);
}
)metal";

struct KernelState final {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  id<MTLComputePipelineState> pipeline = nil;
  QString error;
};

struct HashTableBounds final {
  int min_x = 0;
  int min_y = 0;
  int width = 0;
  int height = 0;
};

HashTableBounds noise_hash_bounds(double time_seconds) {
  constexpr std::array<double, 3> octave_scales{1.0, 2.03, 4.07};
  const double x_start = time_seconds * 0.12;
  const double x_end = x_start + 4.0;
  const double y_start = -time_seconds * 0.08;
  const double y_end = y_start + 4.0;
  int minimum_x = std::numeric_limits<int>::max();
  int minimum_y = std::numeric_limits<int>::max();
  int maximum_x = std::numeric_limits<int>::min();
  int maximum_y = std::numeric_limits<int>::min();
  for (const auto scale : octave_scales) {
    minimum_x = std::min(minimum_x,
                         static_cast<int>(std::floor(std::min(x_start, x_end) * scale)));
    minimum_y = std::min(minimum_y,
                         static_cast<int>(std::floor(std::min(y_start, y_end) * scale)));
    maximum_x = std::max(maximum_x,
                         static_cast<int>(std::floor(std::max(x_start, x_end) * scale)) + 1);
    maximum_y = std::max(maximum_y,
                         static_cast<int>(std::floor(std::max(y_start, y_end) * scale)) + 1);
  }
  --minimum_x;
  --minimum_y;
  ++maximum_x;
  ++maximum_y;
  return {minimum_x, minimum_y, maximum_x - minimum_x + 1,
          maximum_y - minimum_y + 1};
}

QVector<float> make_noise_hash_table(const HashTableBounds& bounds) {
  QVector<float> values(bounds.width * bounds.height);
  for (int row = 0; row < bounds.height; ++row) {
    for (int column = 0; column < bounds.width; ++column) {
      values[row * bounds.width + column] = static_cast<float>(detail::shader_hash(
          static_cast<double>(bounds.min_x + column),
          static_cast<double>(bounds.min_y + row)));
    }
  }
  return values;
}

QVector<float> make_grain_hash_table(int width, int height, int* table_width,
                                     int* table_height) {
  *table_width = static_cast<int>(std::floor((static_cast<double>(width) - 0.5) * 0.7)) + 1;
  *table_height = static_cast<int>(std::floor((static_cast<double>(height) - 0.5) * 0.7)) + 1;
  QVector<float> values(*table_width * *table_height);
  for (int row = 0; row < *table_height; ++row) {
    for (int column = 0; column < *table_width; ++column) {
      values[row * *table_width + column] = static_cast<float>(detail::shader_hash(
          static_cast<double>(column), static_cast<double>(row)));
    }
  }
  return values;
}

id<MTLBuffer> grain_hash_buffer(id<MTLDevice> device, int width, int height,
                                int* table_width, int* table_height) {
  static QMutex mutex;
  static id<MTLBuffer> cached_buffer = nil;
  static int cached_width = 0;
  static int cached_height = 0;
  QMutexLocker lock(&mutex);
  if (cached_buffer != nil && cached_width == width && cached_height == height) {
    *table_width = static_cast<int>(std::floor((static_cast<double>(width) - 0.5) * 0.7)) + 1;
    *table_height = static_cast<int>(std::floor((static_cast<double>(height) - 0.5) * 0.7)) + 1;
    return cached_buffer;
  }
  const auto values = make_grain_hash_table(width, height, table_width, table_height);
  cached_buffer = [device newBufferWithBytes:values.constData()
                                      length:static_cast<NSUInteger>(
                                          static_cast<qsizetype>(values.size()) *
                                          static_cast<qsizetype>(sizeof(float)))
                                     options:MTLResourceStorageModeShared];
  cached_width = cached_buffer == nil ? 0 : width;
  cached_height = cached_buffer == nil ? 0 : height;
  return cached_buffer;
}

KernelState& kernel_state() {
  static KernelState state;
  static dispatch_once_t once_token;
  dispatch_once(&once_token, ^{
    @autoreleasepool {
      state.device = MTLCreateSystemDefaultDevice();
      if (state.device == nil) {
        state.error = QStringLiteral("No Metal device is available.");
        return;
      }
      NSError* error = nil;
      NSString* source = [[NSString alloc] initWithUTF8String:kLightFieldMetalSource];
      MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
      if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeSafe;
      }
      id<MTLLibrary> library = [state.device newLibraryWithSource:source
                                                         options:options
                                                           error:&error];
      if (library == nil) {
        state.error = error == nil
            ? QStringLiteral("Metal could not compile the desktop light shader.")
            : QString::fromNSString(error.localizedDescription);
        return;
      }
      id<MTLFunction> function = [library newFunctionWithName:@"renderDesktopLightField"];
      if (function == nil) {
        state.error = QStringLiteral("The Metal desktop light kernel was not found.");
        return;
      }
      state.pipeline = [state.device newComputePipelineStateWithFunction:function
                                                                    error:&error];
      if (state.pipeline == nil) {
        state.error = error == nil
            ? QStringLiteral("Metal could not create the desktop light pipeline.")
            : QString::fromNSString(error.localizedDescription);
        return;
      }
      state.queue = [state.device newCommandQueue];
      if (state.queue == nil) {
        state.pipeline = nil;
        state.error = QStringLiteral("Metal could not create a command queue.");
      }
    }
  });
  return state;
}

bool was_cancelled(const std::atomic_bool* cancelled) {
  return cancelled != nullptr && cancelled->load(std::memory_order_relaxed);
}

}  // namespace

static QImage render_light_field_metal_impl(
    const QSize& size,
    DesktopLightFieldParameters parameters,
    const std::atomic_bool* cancelled,
    QString* error_message) {
  if (!size.isValid() || size.isEmpty() ||
      !std::isfinite(parameters.time_seconds) ||
      !std::isfinite(parameters.gather) || !std::isfinite(parameters.opacity)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The desktop light frame parameters are invalid.");
    }
    return {};
  }
  if (was_cancelled(cancelled)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The desktop light frame render was cancelled.");
    }
    return {};
  }

  @autoreleasepool {
  auto& state = kernel_state();
  if (state.device == nil || state.queue == nil || state.pipeline == nil) {
    if (error_message != nullptr) {
      *error_message = state.error;
    }
    return {};
  }

  QImage image(size, QImage::Format_RGBA8888);
  if (image.isNull()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The desktop light output image could not be allocated.");
    }
    return {};
  }
  const auto output_bytes = static_cast<NSUInteger>(image.sizeInBytes());
  id<MTLBuffer> output = [state.device newBufferWithLength:output_bytes
                                                   options:MTLResourceStorageModeShared];
  if (output == nil) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Metal could not allocate the desktop light output buffer.");
    }
    return {};
  }

  const auto noise_bounds = noise_hash_bounds(parameters.time_seconds);
  const auto noise_values = make_noise_hash_table(noise_bounds);
  id<MTLBuffer> noise_hashes = [state.device
      newBufferWithBytes:noise_values.constData()
                  length:static_cast<NSUInteger>(
                      static_cast<qsizetype>(noise_values.size()) *
                      static_cast<qsizetype>(sizeof(float)))
                 options:MTLResourceStorageModeShared];
  int grain_width = 0;
  int grain_height = 0;
  id<MTLBuffer> grain_hashes = grain_hash_buffer(
      state.device, size.width(), size.height(), &grain_width, &grain_height);
  if (noise_hashes == nil || grain_hashes == nil) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Metal could not allocate the light field hash tables.");
    }
    return {};
  }

  struct LightParameters final {
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t noise_width;
    uint32_t noise_height;
    uint32_t grain_width;
    uint32_t grain_height;
    int32_t noise_min_x;
    int32_t noise_min_y;
    float time_seconds;
    float gather;
    float opacity;
  } uniforms{
      static_cast<uint32_t>(size.width()),
      static_cast<uint32_t>(size.height()),
      static_cast<uint32_t>(image.bytesPerLine()),
      static_cast<uint32_t>(noise_bounds.width),
      static_cast<uint32_t>(noise_bounds.height),
      static_cast<uint32_t>(grain_width),
      static_cast<uint32_t>(grain_height),
      static_cast<int32_t>(noise_bounds.min_x),
      static_cast<int32_t>(noise_bounds.min_y),
      static_cast<float>(parameters.time_seconds),
      static_cast<float>(std::clamp(parameters.gather, 0.0, 1.0)),
      static_cast<float>(std::clamp(parameters.opacity, 0.0, 1.0)),
  };

  id<MTLCommandBuffer> command_buffer = [state.queue commandBuffer];
  id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
  if (command_buffer == nil || encoder == nil) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Metal could not encode the desktop light frame.");
    }
    return {};
  }
  [encoder setComputePipelineState:state.pipeline];
  [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:0];
  [encoder setBuffer:noise_hashes offset:0 atIndex:1];
  [encoder setBuffer:grain_hashes offset:0 atIndex:2];
  [encoder setBuffer:output offset:0 atIndex:3];
  const MTLSize threadgroup_size = MTLSizeMake(16, 16, 1);
  const MTLSize threadgroup_count = MTLSizeMake(
      (static_cast<NSUInteger>(size.width()) + threadgroup_size.width - 1) /
          threadgroup_size.width,
      (static_cast<NSUInteger>(size.height()) + threadgroup_size.height - 1) /
          threadgroup_size.height,
      1);
  [encoder dispatchThreadgroups:threadgroup_count threadsPerThreadgroup:threadgroup_size];
  [encoder endEncoding];
  [command_buffer commit];
  [command_buffer waitUntilCompleted];
  if (was_cancelled(cancelled)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The desktop light frame render was cancelled.");
    }
    return {};
  }
  if (command_buffer.status != MTLCommandBufferStatusCompleted) {
    if (error_message != nullptr) {
      *error_message = command_buffer.error == nil
          ? QStringLiteral("Metal failed while generating the desktop light frame.")
          : QString::fromNSString(command_buffer.error.localizedDescription);
    }
    return {};
  }

  std::memcpy(image.bits(), output.contents, static_cast<size_t>(image.sizeInBytes()));
  if (error_message != nullptr) {
    error_message->clear();
  }
  return image;
  }
}

QImage render_desktop_light_field_metal(
    const QSize& size,
    DesktopLightFieldParameters parameters,
    const std::atomic_bool* cancelled,
    QString* error_message) {
  return render_light_field_metal_impl(
      size, parameters, cancelled, error_message);
}

QImage render_transition_veil_field_metal(
    const QSize& size,
    double local_time_seconds,
    double light_time_seconds,
    double gather,
    double field_opacity,
    const std::atomic_bool* cancelled,
    QString* error_message) {
  if (!std::isfinite(local_time_seconds)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The transition veil frame time is invalid.");
    }
    return {};
  }
  const DesktopLightFieldParameters parameters{
      .time_seconds = light_time_seconds,
      .gather = gather,
      .opacity = field_opacity,
  };
  auto field = render_light_field_metal_impl(size, parameters, cancelled, error_message);
  if (field.isNull()) {
    return {};
  }
  if (was_cancelled(cancelled)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The desktop light frame render was cancelled.");
    }
    return {};
  }

  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The transition veil output image could not be allocated.");
    }
    return {};
  }
  image.fill(Qt::transparent);
  const auto path = transition_veil_path(QSizeF(size), local_time_seconds);
  if (!path.isEmpty()) {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(1.0);
    painter.setClipPath(path);
    painter.fillRect(image.rect(), QColor(QStringLiteral("#0b0d12")));
    painter.drawImage(QPoint(0, 0), field);
  }
  if (was_cancelled(cancelled)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The desktop light frame render was cancelled.");
    }
    return {};
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  return image.convertToFormat(QImage::Format_RGBA8888);
}

bool desktop_light_field_metal_available() {
  const auto& state = kernel_state();
  return state.device != nil && state.queue != nil && state.pipeline != nil;
}

}  // namespace photo_editor::desktop::timeline_motion
